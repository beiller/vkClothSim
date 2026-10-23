#include "collision_shape_solver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <unordered_map>

namespace css {
namespace {

using Mat3 = std::array<std::array<float, 3>, 3>;

struct Cloud {
    std::vector<Vec3> p;
    std::vector<Vec3> n;
    bool hasNormals = false;
};

Cloud ReadCloud(const float* pos, const float* nrm, std::size_t count) {
    Cloud cloud;
    cloud.p.reserve(count);
    if (nrm != nullptr)
        cloud.n.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        cloud.p.push_back({pos[i * 3 + 0], pos[i * 3 + 1], pos[i * 3 + 2]});
        if (nrm != nullptr)
            cloud.n.push_back(Normalize({nrm[i * 3 + 0], nrm[i * 3 + 1], nrm[i * 3 + 2]}));
    }
    cloud.hasNormals = nrm != nullptr;
    return cloud;
}

Vec3 ComputeCentroid(const std::vector<Vec3>& verts) {
    Vec3 sum{0.f, 0.f, 0.f};
    for (const Vec3& v : verts)
        sum = sum + v;
    return sum * (1.f / (float)verts.size());
}

Mat3 ComputeCovariance(const std::vector<Vec3>& verts, const Vec3& centroid) {
    float xx = 0.f, yy = 0.f, zz = 0.f, xy = 0.f, xz = 0.f, yz = 0.f;
    for (const Vec3& v : verts) {
        const float dx = v.x - centroid.x;
        const float dy = v.y - centroid.y;
        const float dz = v.z - centroid.z;
        xx += dx * dx;
        yy += dy * dy;
        zz += dz * dz;
        xy += dx * dy;
        xz += dx * dz;
        yz += dy * dz;
    }
    const float n = 1.f / (float)verts.size();
    Mat3 cov;
    cov[0] = {xx * n, xy * n, xz * n};
    cov[1] = {xy * n, yy * n, yz * n};
    cov[2] = {xz * n, yz * n, zz * n};
    return cov;
}

// classic Jacobi rotations; values sorted descending, columns of vectors are eigenvectors
void SolveEigens3(const Mat3& input, std::array<float, 3>& outValues, Mat3& outVectors) {
    Mat3 a = input;
    Mat3 v{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            v[i][j] = i == j ? 1.f : 0.f;
    for (int sweep = 0; sweep < 60; ++sweep) {
        float offDiag = 0.f;
        for (int p = 0; p < 3; ++p)
            for (int q = p + 1; q < 3; ++q)
                offDiag += a[p][q] * a[p][q];
        if (offDiag < 1e-18f)
            break;
        for (int p = 0; p < 3; ++p)
            for (int q = p + 1; q < 3; ++q) {
                const float aPQ = a[p][q];
                if (std::fabs(aPQ) < 1e-15f)
                    continue;
                const float theta = 0.5f * std::atan2f(-2.f * aPQ, a[p][p] - a[q][q]);
                const float c = std::cos(theta);
                const float s = std::sin(theta);
                for (int k = 0; k < 3; ++k) {
                    const float akp = a[k][p];
                    const float akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const float apk = a[p][k];
                    const float aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const float vkp = v[k][p];
                    const float vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
    }
    std::array<int, 3> order{0, 1, 2};
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (a[i][i] < a[j][j])
                std::swap(order[i], order[j]);
    for (int i = 0; i < 3; ++i) {
        outValues[i] = a[order[i]][order[i]];
        for (int k = 0; k < 3; ++k)
            outVectors[k][i] = v[k][order[i]];
    }
}

Vec3 ComputePrincipalAxis(const std::vector<Vec3>& verts, const Vec3& centroid, float& eigRatio) {
    const Mat3 covariance = ComputeCovariance(verts, centroid);
    std::array<float, 3> values{};
    Mat3 vectors{};
    SolveEigens3(covariance, values, vectors);
    eigRatio = values[1] > 1e-12f ? values[0] / values[1] : 1e30f;
    Vec3 axis{vectors[0][0], vectors[1][0], vectors[2][0]};
    if (axis.x < 0.f || (std::fabs(axis.x) < 0.9f && axis.y < 0.f))
        axis = axis * -1.f;
    return Normalize(axis);
}

struct AxisProjection {
    Vec3 axis;
    Vec3 origin; // point on the axis at s = 0
    float sMin = 0.f;
    float sMax = 0.f;
};

// projects onto the line through the cloud centroid (not the world origin)
AxisProjection ProjectToAxis(const std::vector<Vec3>& verts, const Vec3& axis, const Vec3& centroid) {
    float minS = 1e30f;
    float maxS = -1e30f;
    for (const Vec3& v : verts) {
        const float s = Dot(v - centroid, axis);
        minS = std::min(minS, s);
        maxS = std::max(maxS, s);
    }
    return {axis, centroid + axis * minS, minS, maxS};
}

float PercentileValue(std::vector<float>& values, float percentile) {
    const std::size_t idx = (std::size_t)std::min((double)values.size() - 1.0, std::ceil(percentile * (double)values.size()) - 1.0);
    std::nth_element(values.begin(), values.begin() + (std::ptrdiff_t)idx, values.end());
    return values[idx];
}

std::vector<float> BuildRadiusProfile(const std::vector<Vec3>& verts, const AxisProjection& projection, const FitOptions& fitOptions) {
    const int binCount = fitOptions.profileBins;
    const float span = projection.sMax - projection.sMin;
    if (span <= 1e-6f)
        return {};
    const Vec3& axis = projection.axis;
    std::vector<std::vector<float>> binRadial(binCount);
    for (const Vec3& v : verts) {
        const float s = Dot(v - projection.origin, axis); // 0 at origin (sMin), span at sMax
        const int bin = std::clamp((int)(s / span * (float)binCount), 0, binCount - 1);
        binRadial[bin].push_back(Length(v - (projection.origin + axis * s)));
    }
    std::vector<float> profile(binCount, -1.f);
    for (int i = 0; i < binCount; ++i)
        if (!binRadial[i].empty())
            profile[i] = PercentileValue(binRadial[i], fitOptions.profilePercentile);
    for (int i = 0; i < binCount; ++i) {
        if (profile[i] >= 0.f)
            continue;
        int lo = i - 1;
        while (lo >= 0 && profile[lo] < 0.f)
            --lo;
        int hi = i + 1;
        while (hi < binCount && profile[hi] < 0.f)
            ++hi;
        if (lo < 0 || hi >= binCount) {
            profile[i] = 0.f;
            continue;
        }
        const float t = (float)(i - lo) / (float)(hi - lo);
        profile[i] = profile[lo] * (1.f - t) + profile[hi] * t;
    }
    // fill isolated dips (a bin below both neighbors): single-bin holes where the surface
    // blended onto an adjacent bone (e.g. the calf bulge). A real ramp / cap tip (high on
    // one side only) is left alone, as are boundary bins.
    for (int w = 1; w <= fitOptions.profileDilate; ++w) {
        const std::vector<float> prev = profile;
        for (int i = w; i < binCount - w; ++i) {
            const float lo = prev[i - w];
            const float hi = prev[i + w];
            if (prev[i] * 1.15f < lo && prev[i] * 1.15f < hi) {
                float mx = prev[i];
                for (int j = i - w; j <= i + w; ++j)
                    mx = std::max(mx, prev[j]);
                profile[i] = mx;
            }
        }
    }
    return profile;
}

struct Line2 {
    float slope = 0.f;
    float intercept = 0.f;
};

Line2 FitLine(const std::vector<float>& profile, int i, int j) {
    const int n = j - i + 1;
    float sx = 0.f, sy = 0.f, sxx = 0.f, sxy = 0.f;
    for (int k = i; k <= j; ++k) {
        const float x = (k + 0.5f) / (float)profile.size();
        sx += x;
        sy += profile[k];
        sxx += x * x;
        sxy += x * profile[k];
    }
    const float xBar = sx / (float)n;
    const float yBar = sy / (float)n;
    const float variance = sxx - (float)n * xBar * xBar;
    const float slope = variance > 1e-12f ? (sxy - (float)n * xBar * yBar) / variance : 0.f;
    return {slope, yBar - slope * xBar};
}

float SegmentCost(const std::vector<float>& profile, int i, int j, const FitOptions& fitOptions) {
    const Line2 line = FitLine(profile, i, j);
    float cost = 0.f;
    for (int k = i; k <= j; ++k) {
        const float x = (k + 0.5f) / (float)profile.size();
        const float e = (line.slope * x + line.intercept) - profile[k]; // positive = fit bigger
        cost += (e > 0.f ? fitOptions.overfitWeight : fitOptions.underfitWeight) * e * e;
    }
    return cost;
}

// dp[m][j] = best weighted cost covering bins [0..j] with m segments; backtracks the segmentation
std::vector<Taper> FitChain(const std::vector<float>& profile, int segmentCount, float length, const FitOptions& fitOptions) {
    const int n = (int)profile.size();
    if (n < 2)
        return {};
    const float infinite = 1e30f;
    std::vector<std::vector<float>> dp(segmentCount + 1, std::vector<float>(n, infinite));
    std::vector<std::vector<int>> parent(segmentCount + 1, std::vector<int>(n, -1));
    for (int m = 1; m <= segmentCount; ++m)
        for (int j = m - 1; j < n; ++j) {
            const int iMin = (m == 1) ? 0 : m - 1;
            const int iMax = (m == 1) ? 0 : j;
            for (int i = iMin; i <= iMax; ++i) {
                const float previous = (m == 1 ? 0.f : dp[m - 1][i - 1]);
                if (previous >= infinite)
                    continue;
                const float cost = previous + SegmentCost(profile, i, j, fitOptions);
                if (cost < dp[m][j]) {
                    dp[m][j] = cost;
                    parent[m][j] = i;
                }
            }
        }
    std::vector<int> starts;
    for (int m = segmentCount, j = n - 1; m >= 1; --m) {
        const int i = parent[m][j];
        if (i < 0)
            return {};
        starts.push_back(i);
        j = i - 1;
    }
    std::reverse(starts.begin(), starts.end());
    std::vector<Taper> tapers;
    for (int m = 0; m < (int)starts.size(); ++m) {
        const int i = starts[m];
        const int j = (m + 1 < (int)starts.size()) ? starts[m + 1] - 1 : n - 1;
        const Line2 line = FitLine(profile, i, j);
        Taper t;
        t.s0 = i / (float)n * length;
        t.s1 = (j + 1) / (float)n * length;
        t.r0 = line.slope * (i / (float)n) + line.intercept;
        t.r1 = line.slope * ((j + 1) / (float)n) + line.intercept;
        tapers.push_back(t);
    }
    for (int m = 1; m < (int)tapers.size(); ++m) {
        const float junction = 0.5f * (tapers[m - 1].r1 + tapers[m].r0);
        tapers[m - 1].r1 = junction;
        tapers[m].r0 = junction;
    }
    return tapers;
}

float RadiusAt(const std::vector<Taper>& tapers, float s) {
    for (const Taper& t : tapers)
        if (s >= t.s0 && s <= t.s1) {
            const float x = (t.s1 - t.s0) > 1e-12f ? (s - t.s0) / (t.s1 - t.s0) : 0.f;
            return t.r0 * (1.f - x) + t.r1 * x;
        }
    if (s <= 0.f)
        return tapers.front().r0;
    return tapers.back().r1;
}

float ComputeProfileScore(const std::vector<Taper>& tapers, const std::vector<float>& profile, float length, const FitOptions& fitOptions) {
    const int n = (int)profile.size();
    float sum = 0.f;
    for (int i = 0; i < n; ++i) {
        const float e = RadiusAt(tapers, (float)i / (float)n * length) - profile[i];
        sum += (e > 0.f ? fitOptions.overfitWeight : fitOptions.underfitWeight) * e * e;
    }
    return std::sqrt(sum / (float)n);
}

float SignedDistanceToShape(const CapsuleFit& fit, const Vec3& p) {
    float sd = 1e30f;
    for (const Taper& t : fit.tapers) {
        const float s = Dot(p - fit.origin, fit.axis);
        const float radial = Length(p - (fit.origin + fit.axis * s));
        if (s >= t.s0 && s <= t.s1) {
            const float x = (t.s1 - t.s0) > 1e-12f ? (s - t.s0) / (t.s1 - t.s0) : 0.f;
            sd = std::min(sd, radial - (t.r0 * (1.f - x) + t.r1 * x));
        }
        sd = std::min(sd, Length(p - (fit.origin + fit.axis * t.s0)) - t.r0);
        sd = std::min(sd, Length(p - (fit.origin + fit.axis * t.s1)) - t.r1);
    }
    return sd;
}

void ComputeShapeError(CapsuleFit& fit, const std::vector<Vec3>& verts) {
    float sumSq = 0.f;
    float sum = 0.f;
    float mx = 0.f;
    for (const Vec3& v : verts) {
        const float sd = SignedDistanceToShape(fit, v);
        sumSq += sd * sd;
        sum += sd;
        mx = std::max(mx, std::fabs(sd));
    }
    const float n = (float)verts.size();
    fit.rmsError = std::sqrt(sumSq / n);
    fit.meanSignedError = sum / n;
    fit.maxError = mx;
}

// uniform-grid spatial hash for nearest-point queries (replaces the O(n) brute-force
// search in the overhang metric). cell size = bbox diagonal / cbrt(n); a query expands
// its neighborhood until the unsearched shell is farther than the best found, which
// guarantees the true nearest (falls through to a full scan for a tiny cloud)
struct SpatialHash {
    float cell = 1e-3f;
    float maxR = 1e30f;
    Vec3 minP{0.f, 0.f, 0.f};
    struct Key {
        int x = 0, y = 0, z = 0;
        bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            return (std::size_t)(k.x * 73856093) ^ (std::size_t)(k.y * 19349663) ^ (std::size_t)(k.z * 83492791);
        }
    };
    std::unordered_map<Key, std::vector<std::size_t>, KeyHash> grid;
    void Build(const std::vector<Vec3>& p) {
        if (p.empty())
            return;
        Vec3 mn = p[0], mx = p[0];
        for (const Vec3& q : p) {
            mn = {std::min(mn.x, q.x), std::min(mn.y, q.y), std::min(mn.z, q.z)};
            mx = {std::max(mx.x, q.x), std::max(mx.y, q.y), std::max(mx.z, q.z)};
        }
        minP = mn;
        const float diag = Length(mx - mn);
        cell = diag / std::cbrt((float)p.size());
        if (cell <= 1e-9f)
            cell = 1e-3f;
        maxR = (float)std::cbrt((float)p.size()) + 2.f;
        grid.reserve(p.size());
        for (std::size_t i = 0; i < p.size(); ++i)
            grid[CellOf(p[i])].push_back(i);
    }
    Key CellOf(const Vec3& q) const {
        return {(int)std::floor((q.x - minP.x) / cell), (int)std::floor((q.y - minP.y) / cell), (int)std::floor((q.z - minP.z) / cell)};
    }
    std::size_t Nearest(const Vec3& q, const std::vector<Vec3>& p) const {
        const std::size_t npos = std::numeric_limits<std::size_t>::max();
        if (grid.empty())
            return npos;
        const Key c = CellOf(q);
        float bestD2 = 1e30f;
        std::size_t best = npos;
        for (float r = 1.f; r <= maxR; ++r) {
            const int ri = (int)r;
            for (int dx = -ri; dx <= ri; ++dx)
                for (int dy = -ri; dy <= ri; ++dy)
                    for (int dz = -ri; dz <= ri; ++dz) {
                        const Key k{c.x + dx, c.y + dy, c.z + dz};
                        const auto it = grid.find(k);
                        if (it == grid.end())
                            continue;
                        for (const std::size_t idx : it->second) {
                            const Vec3 d = q - p[idx];
                            const float d2 = Dot(d, d);
                            if (d2 < bestD2) {
                                bestD2 = d2;
                                best = idx;
                            }
                        }
                    }
            const float unsearched = (r + 0.5f) * cell;
            if (bestD2 <= unsearched * unsearched)
                break;
        }
        return best;
    }
};

// P-percentile overhang of the shape's surface samples (frustum rings + end spheres).
// With normals: the signed distance (p - v)·n beyond the skin (negative = inside, not
// penalized) - a shape in empty space beyond a patch's edge does not overhang, a shape
// poking through the skin does. Without: chord distance to the nearest vertex.
// skipSample (a compound part inside a union) drops surface samples that another
// part covers: they are inside the union, and the nearest-vertex tangent test in
// the gap to that part would flag them as pokes
float ComputeOverhangScore(const std::vector<Taper>& tapers, const Vec3& origin, const Vec3& axis, const Cloud& cloud, const FitOptions& fitOptions, const std::function<bool(const Vec3&)>& skipSample = nullptr) {
    if (cloud.p.empty() || tapers.empty())
        return 0.f;
    const float pi = 3.14159265358979f;
    const Vec3 ref = std::fabs(axis.y) < 0.9f ? Vec3{0.f, 1.f, 0.f} : Vec3{1.f, 0.f, 0.f};
    const Vec3 u = Normalize(Cross(axis, ref));
    const Vec3 v = Cross(axis, u);
    std::vector<float> dist;
    float charR = 0.f;
    for (const Taper& t : tapers)
        charR = std::max({charR, t.r0, t.r1});
    float sMin = 1e30f, sMax = -1e30f;
    for (const Vec3& q : cloud.p) {
        const float s = Dot(q - origin, axis);
        sMin = std::min(sMin, s);
        sMax = std::max(sMax, s);
    }
    SpatialHash hash;
    hash.Build(cloud.p);
    const auto addSample = [&](const Vec3& p, bool cap) {
        if (skipSample && skipSample(p))
            return;
        const std::size_t nearest = hash.Nearest(p, cloud.p);
        if (nearest == std::numeric_limits<std::size_t>::max())
            return;
        const Vec3 d = p - cloud.p[nearest];
        const float bestD2 = Dot(d, d);
        const float value = cloud.hasNormals ? Dot(d, cloud.n[nearest]) : std::sqrt(bestD2);
        // a cap is scored only where the data supports it (near the patch edge); a cap
        // surface in the gap to another part (or past the data) has no support and the
        // nearest-vertex tangent test there is an artifact
        if (cap && std::sqrt(bestD2) > 0.3f * charR)
            return;
        dist.push_back(cloud.hasNormals ? std::max(0.f, value) : value);
    };
    // the frustum body rings are scored too: a bulging profile poking past the far side
    // of the skin is over-coverage and must be seen by the compound score. Body samples
    // exist only inside the data's s range (a shape in empty space beyond the patch edge
    // does not overhang).
    const auto addBody = [&](const Taper& t) {
        if (t.s1 - t.s0 < 1e-9f)
            return;
        for (int i = 0; i < 4; ++i) {
            const float x = (float)(i + 0.5f) / 4.f;
            const float s = t.s0 + (t.s1 - t.s0) * x;
            if (s < sMin || s > sMax)
                continue;
            const float r = t.r0 * (1.f - x) + t.r1 * x;
            for (int j = 0; j < 16; ++j) {
                const float th = 2.f * pi * (float)j / 16.f;
                addSample(origin + axis * s + u * (r * (float)std::cos(th)) + v * (r * (float)std::sin(th)), false);
            }
        }
    };
    for (const Taper& t : tapers)
        addBody(t);
    const auto addCap = [&](const Vec3& c, float r) {
        if (r < 1e-9f)
            return;
        for (int i = 0; i < 40; ++i) {
            const float phi = pi * (float)(i + 0.5f) / 40.f;
            const float th = 2.f * pi * (float)i / 40.f;
            const float ring = (float)std::cos(phi) * r;
            addSample(c + axis * ((float)std::sin(phi) * r) + u * (ring * (float)std::cos(th)) + v * (ring * (float)std::sin(th)), true);
        }
    };
    addCap(origin + axis * tapers.front().s0, tapers.front().r0);
    addCap(origin + axis * tapers.back().s1, tapers.back().r1);
    return dist.empty() ? 0.f : PercentileValue(dist, fitOptions.overhangPercentile);
}

// least-squares sphere: minimizes sum (|p_i - c|² - R²)² via the 4x4 normal equations for
// (cx, cy, cz, D) with D = R² - |c|². Centers the points first for conditioning.
bool FitSphere(const std::vector<Vec3>& verts, Vec3& outCenter, float& outRadius) {
    if (verts.size() < 4)
        return false;
    const Vec3 c0 = ComputeCentroid(verts);
    double m00 = 0, m01 = 0, m02 = 0, m03 = 0, m11 = 0, m12 = 0, m13 = 0, m22 = 0, m23 = 0, m33 = 0;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    for (const Vec3& p : verts) {
        const double x = p.x - c0.x, y = p.y - c0.y, z = p.z - c0.z;
        const double s = x * x + y * y + z * z;
        const double r0 = 2 * x, r1 = 2 * y, r2 = 2 * z, r3 = 1.0;
        m00 += r0 * r0; m01 += r0 * r1; m02 += r0 * r2; m03 += r0 * r3;
        m11 += r1 * r1; m12 += r1 * r2; m13 += r1 * r3;
        m22 += r2 * r2; m23 += r2 * r3;
        m33 += r3 * r3;
        b0 += r0 * s; b1 += r1 * s; b2 += r2 * s; b3 += r3 * s;
    }
    double M[4][4] = {{m00, m01, m02, m03}, {m01, m11, m12, m13}, {m02, m12, m22, m23}, {m03, m13, m23, m33}};
    double B[4] = {b0, b1, b2, b3};
    for (int i = 0; i < 4; ++i) {
        int piv = i;
        double mx = std::fabs(M[i][i]);
        for (int r = i + 1; r < 4; ++r)
            if (std::fabs(M[r][i]) > mx) { mx = std::fabs(M[r][i]); piv = r; }
        if (mx < 1e-12)
            return false;
        if (piv != i) {
            for (int c = i; c < 4; ++c)
                std::swap(M[i][c], M[piv][c]);
            std::swap(B[i], B[piv]);
        }
        for (int r = i + 1; r < 4; ++r) {
            const double f = M[r][i] / M[i][i];
            for (int c = i; c < 4; ++c)
                M[r][c] -= f * M[i][c];
            B[r] -= f * B[i];
        }
    }
    double x[4];
    for (int i = 3; i >= 0; --i) {
        x[i] = B[i];
        for (int c = i + 1; c < 4; ++c)
            x[i] -= M[i][c] * x[c];
        x[i] /= M[i][i];
    }
    const float cx = (float)(x[0] + c0.x), cy = (float)(x[1] + c0.y), cz = (float)(x[2] + c0.z);
    const float offset2 = x[0] * x[0] + x[1] * x[1] + x[2] * x[2];
    const float r2 = (float)x[3] + offset2;
    if (r2 <= 1e-12f)
        return false;
    outCenter = Vec3{cx, cy, cz};
    outRadius = std::sqrt(r2);
    return true;
}

// inward depth of each surface point along its own normal, measured from the given center
std::vector<float> NormalDepths(const std::vector<Vec3>& verts, const std::vector<Vec3>& nrm, const Vec3& center) {
    std::vector<float> depth;
    depth.reserve(verts.size());
    for (std::size_t i = 0; i < verts.size(); ++i)
        depth.push_back(Dot(verts[i] - center, nrm[i]));
    return depth;
}

// the body center from the normals: c* = argmin_c Var_v[(v - c)·n_v], the point from which
// the surface is at the most uniform inward depth (the ball center). Solves
// (S - tt^T/N) c = M - At/N with S = sum n n^T, M = sum (p·n) n, t = sum n, A = sum (p·n),
// points centered first for conditioning. Singular (a flat patch) is rejected.
bool FitNormalCenter(const std::vector<Vec3>& verts, const std::vector<Vec3>& nrm, Vec3& outCenter) {
    if (verts.size() < 4 || nrm.size() != verts.size())
        return false;
    const Vec3 c0 = ComputeCentroid(verts);
    const int n = (int)verts.size();
    double S[3][3] = {{0}};
    double M[3] = {0};
    double A = 0.0;
    double t[3] = {0};
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const double px = verts[i].x - c0.x, py = verts[i].y - c0.y, pz = verts[i].z - c0.z;
        const double nx = nrm[i].x, ny = nrm[i].y, nz = nrm[i].z;
        const double a = px * nx + py * ny + pz * nz;
        A += a;
        const double b[3] = {nx, ny, nz};
        for (int r = 0; r < 3; ++r) {
            M[r] += a * b[r];
            t[r] += b[r];
            for (int c = 0; c < 3; ++c)
                S[r][c] += b[r] * b[c];
        }
    }
    double Mm[3][3];
    double B[3];
    double scale = 0.0;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            Mm[r][c] = S[r][c] - t[r] * t[c] / (double)n;
            scale = std::max(scale, std::fabs(Mm[r][c]));
        }
        B[r] = M[r] - A * t[r] / (double)n;
    }
    for (int i = 0; i < 3; ++i) {
        int piv = i;
        double mx = std::fabs(Mm[i][i]);
        for (int r = i + 1; r < 3; ++r)
            if (std::fabs(Mm[r][i]) > mx) {
                mx = std::fabs(Mm[r][i]);
                piv = r;
            }
        if (mx < 1e-12 * scale)
            return false;
        if (piv != i) {
            for (int c = 0; c < 3; ++c)
                std::swap(Mm[i][c], Mm[piv][c]);
            std::swap(B[i], B[piv]);
        }
        for (int r = i + 1; r < 3; ++r) {
            const double f = Mm[r][i] / Mm[i][i];
            for (int c = i; c < 3; ++c)
                Mm[r][c] -= f * Mm[i][c];
            B[r] -= f * B[i];
        }
    }
    double x[3];
    for (int i = 2; i >= 0; --i) {
        x[i] = B[i];
        for (int c = i + 1; c < 3; ++c)
            x[i] -= Mm[i][c] * x[c];
        x[i] /= Mm[i][i];
    }
    outCenter = c0 + Vec3{(float)x[0], (float)x[1], (float)x[2]};
    return true;
}

struct BodyFrame {
    Vec3 center;
    std::vector<float> depth; // corrected inward depth (>= 0 on average)
    bool flipped = false;    // the normals pointed inward; signs corrected
};

// resolves the body center + inward depths. The center is the normal center of curvature,
// falling back to the centroid when the normal solve is degenerate (a flat patch). A
// negative mean depth means the normals point inward; signs are corrected.
BodyFrame ResolveBodyFrame(const std::vector<Vec3>& verts, const std::vector<Vec3>& nrm) {
    BodyFrame frame;
    if (!FitNormalCenter(verts, nrm, frame.center))
        frame.center = ComputeCentroid(verts);
    frame.depth = NormalDepths(verts, nrm, frame.center);
    float mean = 0.f;
    for (const float d : frame.depth)
        mean += d;
    mean /= (float)frame.depth.size();
    frame.flipped = mean < 0.f;
    if (frame.flipped)
        for (float& d : frame.depth)
            d = -d;
    return frame;
}

// offsets each surface point inward along its own normal by its own depth: lands on the
// body centerline (exact for a tube)
std::vector<Vec3> CenterlinePoints(const std::vector<Vec3>& verts, const std::vector<Vec3>& nrm, const std::vector<float>& depth, bool flipped) {
    const float sign = flipped ? 1.f : -1.f;
    std::vector<Vec3> pts;
    pts.reserve(verts.size());
    for (std::size_t i = 0; i < verts.size(); ++i)
        pts.push_back(verts[i] + nrm[i] * (depth[i] * sign));
    return pts;
}

// fits the tapered chain to the radius profile and finalizes the fit struct
bool FitChainToProfile(CapsuleFit& fit, const Cloud& cloud, const AxisProjection& projection, float length, float meanRadius, const FitOptions& fitOptions) {
    const std::vector<float> profile = BuildRadiusProfile(cloud.p, projection, fitOptions);
    if (profile.size() < 2)
        return false;
    if (getenv("CSS_PROFILE_DEBUG"))
        for (std::size_t i = 0; i < profile.size(); ++i)
            std::printf("  [profile bin %3zu  %.4f]\n", i, profile[i]);
    fit.origin = projection.origin;
    fit.length = length;
    std::vector<Taper> bestTapers;
    float bestScore = 1e30f;
    float bestProfile = 0.f;
    float bestOverhang = 0.f;
    for (int k = 1; k <= std::min(fitOptions.maxSegments, (int)profile.size()); ++k) {
        const std::vector<Taper> chain = FitChain(profile, k, length, fitOptions);
        if (chain.empty())
            continue;
        const float profileScore = ComputeProfileScore(chain, profile, length, fitOptions);
        const float overhang = ComputeOverhangScore(chain, fit.origin, fit.axis, cloud, fitOptions);
        const float score = profileScore + fitOptions.overhangWeight * overhang;
        if (getenv("CSS_PROFILE_DEBUG"))
            std::printf("  [k %d  profile %.5f  overhang %.5f  score %.5f  threshold %.5f]\n", k, profileScore, overhang, score,
                       fitOptions.tolerance * meanRadius);
        if (score < bestScore) {
            bestScore = score;
            bestProfile = profileScore;
            bestOverhang = overhang;
            bestTapers = chain;
        }
        if (profileScore <= fitOptions.tolerance * meanRadius)
            break;
    }
    if (bestTapers.empty())
        return false;
    fit.tapers = bestTapers;
    fit.segments = (int)bestTapers.size();
    fit.profileScore = bestProfile;
    fit.overhangScore = bestOverhang;
    ComputeShapeError(fit, cloud.p);
    fit.ok = true;
    return true;
}

// positions-only fit (no normals): PCA axis of the shell, chord radii, least-squares sphere
CapsuleFit FitPositionsOnly(const Cloud& cloud, const FitOptions& fitOptions) {
    CapsuleFit fit;
    const Vec3 centroid = ComputeCentroid(cloud.p);
    float eigRatio = 1e30f;
    const Vec3 axis = ComputePrincipalAxis(cloud.p, centroid, eigRatio);
    const AxisProjection projection = ProjectToAxis(cloud.p, axis, centroid);
    const float length = projection.sMax - projection.sMin;
    if (length <= 1e-6f)
        return fit;
    fit.axis = axis;
    fit.axisEigRatio = eigRatio;
    const std::vector<float> profile = BuildRadiusProfile(cloud.p, projection, fitOptions);
    if (profile.size() < 2)
        return fit;
    float meanRadius = 0.f;
    for (const float r : profile)
        meanRadius += r;
    meanRadius /= (float)profile.size();
    if (meanRadius <= 1e-9f)
        return fit;
    if (length / meanRadius < fitOptions.sphereAspect) {
        // blob-like cloud (aspect ~ 2, no principal direction): a capsule's length would just
        // bulge, so the optimal shape is a sphere. A length-0 taper with r0 = r1 = R is a sphere
        // in the SDF, the mesh, and Jolt; center it on the centroid, radius = P90 distance from
        // the centroid
        Vec3 center;
        float R;
        if (!FitSphere(cloud.p, center, R) || R <= 1e-9f)
            return fit;
        fit.origin = center;
        fit.length = 0.f;
        Taper ball;
        ball.s0 = 0.f;
        ball.s1 = 0.f;
        ball.r0 = R;
        ball.r1 = R;
        fit.tapers.push_back(ball);
        fit.segments = 1;
        float psum = 0.f;
        std::vector<float> dists;
        dists.reserve(cloud.p.size());
        for (const Vec3& v : cloud.p) {
            const float d = Length(v - center);
            dists.push_back(d);
            const float e = R - d;
            psum += e * e;
        }
        if (getenv("CSS_SPHERE_DEBUG")) {
            std::sort(dists.begin(), dists.end());
            const auto pctl = [&](float q) { return dists[(std::size_t)(q * (dists.size() - 1))]; };
            std::printf("  [spherespread min %.3f P10 %.3f P50 %.3f P90 %.3f P99 %.3f max %.3f  R %.3f n %zu]\n",
                       dists.front(), pctl(0.1f), pctl(0.5f), pctl(0.9f), pctl(0.99f), dists.back(), R, dists.size());
        }
        fit.profileScore = std::sqrt(psum / (float)dists.size());
        fit.overhangScore = ComputeOverhangScore(fit.tapers, fit.origin, fit.axis, cloud, fitOptions);
        ComputeShapeError(fit, cloud.p);
        fit.ok = true;
        return fit;
    }
    FitChainToProfile(fit, cloud, projection, length, meanRadius, fitOptions);
    return fit;
}

// chain fit constrained to a given axis (the rough / bone frame). The axis is not
// PCA-derived, so a wide flat cap (the calf) cannot degenerate the frame the way
// its own PCA does; the chain runs along the bone axis and the P100 profile gives
// the bulge capsule
CapsuleFit FitToFrame(const Cloud& cloud, const Vec3& frameAxis, const FitOptions& fitOptions) {
    CapsuleFit fit;
    const Vec3 centroid = ComputeCentroid(cloud.p);
    const AxisProjection projection = ProjectToAxis(cloud.p, frameAxis, centroid);
    const float length = projection.sMax - projection.sMin;
    if (length <= 1e-6f)
        return fit;
    fit.axis = frameAxis;
    fit.axisEigRatio = 1e30f;
    const std::vector<float> profile = BuildRadiusProfile(cloud.p, projection, fitOptions);
    float meanRadius = 0.f;
    for (const float r : profile)
        meanRadius += r;
    meanRadius /= (float)profile.size();
    if (meanRadius <= 1e-9f)
        return fit;
    FitChainToProfile(fit, cloud, projection, length, meanRadius, fitOptions);
    return fit;
}

// normal-aware fit: the axis follows the body centerline (each point offset inward along
// its own normal) instead of the PCA of the shell; a blob-like cloud becomes a sphere
// centered at the normal center of curvature, radius = P90 inward depth along the normals
// (the true radial distance, not a chord)
CapsuleFit FitNormalAware(const Cloud& cloud, const FitOptions& fitOptions) {
    CapsuleFit fit;
    BodyFrame frame = ResolveBodyFrame(cloud.p, cloud.n);
    const std::vector<Vec3> centerline = CenterlinePoints(cloud.p, cloud.n, frame.depth, frame.flipped);
    float eigRatio = 1e30f;
    const Vec3 centerlineCentroid = ComputeCentroid(centerline);
    const Vec3 axis = ComputePrincipalAxis(centerline, centerlineCentroid, eigRatio);
    const AxisProjection projection = ProjectToAxis(cloud.p, axis, centerlineCentroid);
    const float length = projection.sMax - projection.sMin;
    if (length <= 1e-6f)
        return fit;
    fit.axis = axis;
    fit.axisEigRatio = eigRatio;
    float meanRadius = 0.f;
    for (const float d : frame.depth)
        meanRadius += d;
    meanRadius /= (float)frame.depth.size();
    if (meanRadius <= 1e-9f)
        return fit;
    if (length / meanRadius < fitOptions.sphereAspect) {
        // blob-like cloud: the optimal shape is a sphere (a length-0 taper with r0 = r1 = R
        // is a sphere in the SDF, the mesh, and Jolt), centered at the normal center of
        // curvature, radius = robust percentile of the inward depth along the normals
        const float R = PercentileValue(frame.depth, fitOptions.spherePercentile);
        if (R <= 1e-9f)
            return fit;
        fit.origin = frame.center;
        fit.length = 0.f;
        Taper ball;
        ball.s0 = 0.f;
        ball.s1 = 0.f;
        ball.r0 = R;
        ball.r1 = R;
        fit.tapers.push_back(ball);
        fit.segments = 1;
        float psum = 0.f;
        std::vector<float> dists = frame.depth;
        for (const float d : dists) {
            const float e = R - d;
            psum += e * e;
        }
        if (getenv("CSS_SPHERE_DEBUG")) {
            std::sort(dists.begin(), dists.end());
            const auto pctl = [&](float q) { return dists[(std::size_t)(q * (dists.size() - 1))]; };
            std::printf("  [spherespread min %.3f P10 %.3f P50 %.3f P90 %.3f P99 %.3f max %.3f  R %.3f n %zu]\n",
                       dists.front(), pctl(0.1f), pctl(0.5f), pctl(0.9f), pctl(0.99f), dists.back(), R, dists.size());
        }
        fit.profileScore = std::sqrt(psum / (float)dists.size());
        fit.overhangScore = ComputeOverhangScore(fit.tapers, fit.origin, fit.axis, cloud, fitOptions);
        ComputeShapeError(fit, cloud.p);
        fit.ok = true;
        return fit;
    }
    FitChainToProfile(fit, cloud, projection, length, meanRadius, fitOptions);
    return fit;
}

// union of a ring (tube) fit plus one tapered-capsule chain per protruding surface
// region (a bulge). Two-pass: a rough flagging in the base-fit frame (threshold 1.3x
// the ring median, above the base axis's bulge-induced skew) locates the bulge cells,
// which are removed before the ring is refitted (positions-only, P90 profile) for a
// clean frame; a tight pass (1.1x) in the clean frame cuts the bulge shell. Each
// region's shell gets its own fit and is added greedily while the score improves.
// No bulge found degenerates to the plain single-cloud fit.
CompoundFit FitCloudCompound(const Cloud& cloud, const FitOptions& fitOptions) {
    struct Score3 {
        float underfit = 0.f;
        float overhang = 0.f;
        float score = 0.f;
    };
    const auto scoreParts = [&](const std::vector<CapsuleFit>& parts, const std::vector<std::vector<int>>& partIdx) -> Score3 {
        std::vector<float> uncovered;
        uncovered.reserve(cloud.p.size());
        for (const Vec3& v : cloud.p) {
            float d = 1e30f;
            for (const CapsuleFit& part : parts)
                d = std::min(d, SignedDistanceToShape(part, v));
            uncovered.push_back(std::max(0.f, d));
        }
        // per-part overhang: a part is scored only against its own vertices not covered
        // by the other parts. A part's end cap filling the concave gap to another part
        // is inside the union (not a poke), but the nearest-vertex tangent test at the
        // other part's skin would flag it.
        Score3 s;
        s.underfit = PercentileValue(uncovered, fitOptions.overhangPercentile);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            Cloud keep;
            keep.p.reserve(partIdx[i].size());
            if (cloud.hasNormals)
                keep.n.reserve(partIdx[i].size());
            for (const int v : partIdx[i]) {
                bool covered = false;
                for (std::size_t j = 0; j < parts.size() && !covered; ++j)
                    if (j != i && SignedDistanceToShape(parts[j], cloud.p[v]) < 0)
                        covered = true;
                if (!covered) {
                    keep.p.push_back(cloud.p[v]);
                    if (cloud.hasNormals)
                        keep.n.push_back(cloud.n[v]);
                }
            }
            keep.hasNormals = cloud.hasNormals;
            auto skip = [&parts, i](const Vec3& p) {
                for (std::size_t j = 0; j < parts.size(); ++j)
                    if (j != i && SignedDistanceToShape(parts[j], p) < 0)
                        return true;
                return false;
            };
            s.overhang += ComputeOverhangScore(parts[i].tapers, parts[i].origin, parts[i].axis, keep, fitOptions, skip);
        }
        s.score = fitOptions.underfitWeight * s.underfit + fitOptions.overhangWeight * s.overhang;
        return s;
    };
    const auto subCloudFit = [&](const std::vector<int>& idx, const FitOptions& opts, bool positionsOnly) {
        Cloud sub;
        sub.p.reserve(idx.size());
        if (cloud.hasNormals)
            sub.n.reserve(idx.size());
        for (const int i : idx) {
            sub.p.push_back(cloud.p[i]);
            if (cloud.hasNormals)
                sub.n.push_back(cloud.n[i]);
        }
        sub.hasNormals = cloud.hasNormals;
        if (positionsOnly)
            return FitPositionsOnly(sub, opts);
        return cloud.hasNormals ? FitNormalAware(sub, opts) : FitPositionsOnly(sub, opts);
    };

    CompoundFit best;
    const CapsuleFit base = cloud.hasNormals ? FitNormalAware(cloud, fitOptions) : FitPositionsOnly(cloud, fitOptions);
    if (!base.ok)
        return best;
    best.parts.push_back(base);
    std::vector<int> allIdx;
    allIdx.reserve(cloud.p.size());
    for (std::size_t i = 0; i < cloud.p.size(); ++i)
        allIdx.push_back((int)i);
    const Score3 baseScore = scoreParts(best.parts, {allIdx});
    best.underfit = baseScore.underfit;
    best.overhang = baseScore.overhang;
    best.score = baseScore.score;
    best.ok = true;
    if (base.length <= 1e-6f)
        return best; // blob-like: no ring frame to segment

    // one flagging + region pass in a given frame. The ring baseline is the P10 radius
    // of the s band (the ring band holds the lower quartile even when a bulge covers
    // most of the band, because a bulge's radius starts at the ring's); a point is a
    // bulge point above excessFactor x the baseline (1.15, chosen so the exclusion
    // cut stays below the inner 10% of a bulge and the tube P90 stays on the ring). A region is a connected set of
    // bulge cells (ring-mesh fragments merged into the whole bulge); its part is the
    // outer shell (bulge points) only, and the whole cells are removed from the tube
    // so the tube fit stays a clean ring.
    struct Region {
        std::vector<int> cells;
        std::vector<int> shell; // flagged (bulge) points; the size gate and the part fit
    };
    struct RegionPass {
        std::vector<Region> regions;
        std::vector<char> excluded;
    };
    const int sBins = 64, tBins = 32;
    const auto makeRegions = [&](const std::vector<int>& idx, const CapsuleFit& frame, float excessFactor) -> RegionPass {
        RegionPass out;
        out.excluded.assign(cloud.p.size(), 0);
        const Vec3 ref = std::fabs(frame.axis.y) < 0.9f ? Vec3{0.f, 1.f, 0.f} : Vec3{1.f, 0.f, 0.f};
        const Vec3 u = Normalize(Cross(frame.axis, ref));
        const Vec3 v = Cross(frame.axis, u);
        // normalize s by the data's own extent along the frame axis (like the radius
        // profile does): the frame's length can be 0 (a blob-like fit) or differ
        // from the data's extent, which would clamp half the cloud into one band
        float sMin = 1e30f, sMax = -1e30f;
        for (const int gi : idx) {
            const float s = Dot(cloud.p[gi] - frame.origin, frame.axis);
            sMin = std::min(sMin, s);
            sMax = std::max(sMax, s);
        }
        if (sMax - sMin < 1e-6f)
            return out;
        std::vector<int> ptCell(idx.size());
        std::vector<float> ptR(idx.size());
        std::vector<std::vector<int>> cellPts((size_t)sBins * tBins);
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const Vec3 d = cloud.p[idx[i]] - frame.origin;
            const float s = Dot(d, frame.axis);
            const Vec3 w = d - frame.axis * s;
            const int sb = std::clamp((int)((s - sMin) / (sMax - sMin) * (float)sBins), 0, sBins - 1);
            const int tb = std::clamp((int)((std::atan2f(Dot(w, v), Dot(w, u)) + 3.14159265f) / 6.28318530718f * (float)tBins), 0,
                                     tBins - 1);
            ptCell[i] = (size_t)sb * tBins + tb;
            ptR[i] = Length(w);
            cellPts[ptCell[i]].push_back((int)i);
        }
        // per-s-band (11-s x full-theta) P25 ring baseline: the ring band holds the
        // lower quartile even when a bulge covers most of the band, because a bulge's
        // radius starts at the ring's. Full-theta so a wide sector bulge (the calf)
        // keeps the ring in its baseline and is flagged, while a ring-wide flare (the
        // knee condyles) matches the baseline and stays in the tube. The 11-s window
        // smooths the bone's taper so the threshold stays above the ring.
        std::vector<float> bandBase(sBins, 0.f);
        for (int sb = 0; sb < sBins; ++sb) {
            std::vector<float> rs;
            for (int ds = -5; ds <= 5; ++ds) {
                const int ss = sb + ds;
                if (ss < 0 || ss >= sBins)
                    continue;
                for (int tb = 0; tb < tBins; ++tb)
                    for (const int p : cellPts[(size_t)ss * tBins + tb])
                        rs.push_back(ptR[p]);
            }
            if (rs.size() >= 8)
                bandBase[sb] = PercentileValue(rs, 0.25f);
        }
        if (getenv("CSS_COMPOUND_DEBUG"))
            for (int sb : {0, 10, 20, 30, 40, 60}) {
                if (bandBase[sb] > 0.f)
                    std::printf("  [band %d  base %.4f  thr %.4f]\n", sb, bandBase[sb], bandBase[sb] * excessFactor);
            }
        std::vector<char> bulgePt(idx.size(), 0);
        std::vector<char> flagged((size_t)sBins * tBins, 0);
        int nBulge = 0;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const float b = bandBase[ptCell[i] / tBins];
            if (b > 0.f && ptR[i] > b * excessFactor) {
                bulgePt[i] = 1;
                flagged[ptCell[i]] = 1;
                ++nBulge;
            }
        }
        if (getenv("CSS_COMPOUND_DEBUG")) {
            std::printf("  [frame uv u %.3f %.3f %.3f  v %.3f %.3f %.3f]\n", u.x, u.y, u.z, v.x, v.y, v.z);
            int printed = 0;
            for (std::size_t i = 0; i < idx.size() && printed < 3; ++i)
                if (bulgePt[i]) {
                    const Vec3& p = cloud.p[idx[i]];
                    std::printf("  [flag pt %.3f %.3f %.3f  r %.4f  cell %d]\n", p.x, p.y, p.z, ptR[i], ptCell[i]);
                    ++printed;
                }
            int nPosY = 0, nNegY = 0;
            float maxR = 0.f;
            for (std::size_t i = 0; i < idx.size(); ++i)
                if (bulgePt[i]) {
                    const Vec3& p = cloud.p[idx[i]];
                    if (p.y > 0.f)
                        ++nPosY;
                    else
                        ++nNegY;
                    maxR = std::max(maxR, ptR[i]);
                }
            std::printf("  [flag split posY %d negY %d  maxR %.4f]\n", nPosY, nNegY, maxR);
        }
        if (getenv("CSS_COMPOUND_DEBUG")) {
            std::printf("  [pass flag %d pts  factor %.2f]\n", nBulge, excessFactor);
            for (int sb = 0; sb < sBins; ++sb) {
                int cnt = 0;
                for (int tb = 0; tb < tBins; ++tb)
                    for (const int p : cellPts[(size_t)sb * tBins + tb])
                        if (bulgePt[p])
                            ++cnt;
                if (cnt > 0)
                    std::printf("  [bin %d  flagged %d]\n", sb, cnt);
            }
            for (int sb = 0; sb < sBins; ++sb) {
                std::string line;
                for (int tb = 0; tb < tBins; ++tb)
                    line += flagged[(size_t)sb * tBins + tb] ? "#" : ".";
                std::printf("  [grid %2d %s]\n", sb, line.c_str());
            }
            if (getenv("CSS_COMP_SECTORS")) {
                for (int sb : {16, 32, 45}) {
                    std::printf("  [sectors band %d]\n", sb);
                    for (int tb = 0; tb < tBins; tb += 4) {
                        std::vector<float> rs;
                        for (int dt = 0; dt < 4; ++dt)
                            for (const int p : cellPts[(size_t)sb * tBins + (tb + dt) % tBins])
                                rs.push_back(ptR[p]);
                        if (!rs.empty())
                            std::printf("  [  th %2d-%2d  n %3d  p90 %.4f  max %.4f]\n", tb, tb + 3, (int)rs.size(),
                                       PercentileValue(rs, 0.9f), *std::max_element(rs.begin(), rs.end()));
                    }
                }
            }
        }
        std::vector<char> seen((size_t)sBins * tBins, 0);
        for (int c = 0; c < sBins * tBins; ++c) {
            if (!flagged[c] || seen[c])
                continue;
            Region rg;
            std::vector<int> stack{c};
            seen[c] = 1;
            while (!stack.empty()) {
                const int cur = stack.back();
                stack.pop_back();
                rg.cells.push_back(cur);
                const int sb = cur / tBins;
                const int tb = cur % tBins;
                const int nb[4] = {sb > 0 ? cur - tBins : -1,
                                  sb + 1 < sBins ? cur + tBins : -1,
                                  sb * tBins + (tb + 1) % tBins,
                                  sb * tBins + (tb + tBins - 1) % tBins};
                for (const int n2 : nb)
                    if (n2 >= 0 && !seen[n2] && flagged[n2]) {
                        seen[n2] = 1;
                        stack.push_back(n2);
                    }
            }
            for (const int cell : rg.cells)
                for (const int p : cellPts[cell])
                    if (bulgePt[p])
                        rg.shell.push_back(idx[p]);
            // fragment floor (small: a ring-mesh bulge arrives per vertex row, a few
            // cells wide); the merge below joins the fragments and the 200 gate and
            // the normal check keep ring noise out
            if (rg.shell.size() >= 5)
                out.regions.push_back(std::move(rg));
        }
        // a ring-mesh bulge arrives in per-vertex-row fragments a band or two
        // apart; join fragments whose cells are within 2 bands and whose mean
        // normals agree, so the exclusion and the part fit see the whole bulge
        if (cloud.hasNormals && out.regions.size() > 1) {
            const auto regionMeanNormal = [&](const Region& rg) -> Vec3 {
                Vec3 n{0.f, 0.f, 0.f};
                for (const int p : rg.shell)
                    n = n + cloud.n[p];
                return Normalize(n);
            };
            // the s gap between the fragments (with a theta tolerance: a row's cells
            // fragment across unflagged theta gaps); the ring mesh leaves 2-3 band
            // gaps between bulge rows, so <= 3 bands apart joins
            const auto cellGap = [&](const Region& a, const Region& b) -> int {
                std::vector<char> grid((size_t)sBins * tBins, 0);
                for (const int c : b.cells)
                    grid[c] = 1;
                int best = 4;
                for (const int c : a.cells) {
                    const int sb = c / tBins;
                    const int tb = c % tBins;
                    for (int ds = -3; ds <= 3 && best > 3; ++ds) {
                        const int ss = sb + ds;
                        if (ss < 0 || ss >= sBins)
                            continue;
                        for (int dt = -4; dt <= 4; ++dt)
                            if (grid[ss * tBins + (tb + dt + tBins) % tBins])
                                best = std::min(best, std::abs(ds));
                    }
                }
                return best;
            };
            std::vector<Vec3> meanN;
            for (const auto& rg : out.regions)
                meanN.push_back(regionMeanNormal(rg));
            if (getenv("CSS_COMPOUND_DEBUG")) {
                std::printf("  [merge raw regions %zu]", out.regions.size());
                for (const auto& rg : out.regions)
                    std::printf(" (%zu)", rg.shell.size());
                std::printf("\n");
            }
            if (getenv("CSS_COMPOUND_DEBUG")) {
                for (int i = 0; i < (int)out.regions.size(); ++i)
                    for (int j = i + 1; j < (int)out.regions.size(); ++j) {
                        const int gap = cellGap(out.regions[i], out.regions[j]);
                        if (gap <= 3)
                            std::printf("  [pair %zu(%zu) %zu(%zu)  gap %d  dot %.2f]\n", i, out.regions[i].shell.size(), j,
                                       out.regions[j].shell.size(), gap, Dot(meanN[i], meanN[j]));
                    }
            }
            std::vector<char> dead(out.regions.size(), 0);
            bool changed = true;
            while (changed) {
                changed = false;
                for (int i = 0; i < (int)out.regions.size(); ++i) {
                    if (dead[i])
                        continue;
                    for (int j = i + 1; j < (int)out.regions.size(); ++j) {
                        if (dead[j])
                            continue;
                        if (Dot(meanN[i], meanN[j]) > 0.5f && cellGap(out.regions[i], out.regions[j]) <= 3) {
                            if (getenv("CSS_COMPOUND_DEBUG"))
                                std::printf("  [merge %zu(%zu) + %zu(%zu)  dot %.2f]\n", i, out.regions[i].shell.size(), j,
                                           out.regions[j].shell.size(), Dot(meanN[i], meanN[j]));
                            for (const int c : out.regions[j].cells)
                                if (std::find(out.regions[i].cells.begin(), out.regions[i].cells.end(), c) ==
                                    out.regions[i].cells.end())
                                    out.regions[i].cells.push_back(c);
                            out.regions[i].shell.insert(out.regions[i].shell.end(), out.regions[j].shell.begin(),
                                                      out.regions[j].shell.end());
                            meanN[i] = regionMeanNormal(out.regions[i]);
                            dead[j] = 1;
                            changed = true;
                        }
                    }
                }
            }
            std::vector<Region> kept;
            for (int i = 0; i < (int)out.regions.size(); ++i)
                if (!dead[i])
                    kept.push_back(std::move(out.regions[i]));
            out.regions = std::move(kept);
            if (getenv("CSS_COMPOUND_DEBUG"))
                for (const auto& rg : out.regions) {
                    int lo = sBins, hi = -1;
                    for (const int c : rg.cells) {
                        lo = std::min(lo, c / tBins);
                        hi = std::max(hi, c / tBins);
                    }
                    std::printf("  [region bands %d-%d  %zu pts]\n", lo, hi, rg.shell.size());
                }
        }
        std::vector<Region> gated;
        for (auto& rg : out.regions)
            if (rg.shell.size() >= 200)
                gated.push_back(std::move(rg));
        out.regions = std::move(gated);
        for (const auto& rg : out.regions)
            for (const int cell : rg.cells)
                for (const int p : cellPts[cell])
                    out.excluded[idx[p]] = 1;
        if (getenv("CSS_COMPOUND_DEBUG")) {
            std::printf("  [pass regions %zu", out.regions.size());
            for (const auto& rg : out.regions)
                std::printf(" (%zu shell)", rg.shell.size());
            std::printf("]\n");
        }
        return out;
    };
    // region pass in a positions-only (PCA) frame: the normal-aware base axis is
    // pulled toward the bulge, compressing the bulge's excess and skewing the ring
    // radius; the PCA frame skews the ring by <2%, so the 1.15x-over-P25 threshold
    // sits above the ring band and flags the bulge cells (the tube's P90 profile
    // cannot stand a half-contaminated band; the cut must also stay below the
    // inner 10% of the bulge, or the tube P90 inflates to the bulge's). The
    // region cells are removed and the ring refitted (positions-only) for the tube;
    // the part fits use the region shells directly (normal-aware, in world space,
    // frame independent). A second pass in the tube frame was tried and rejected:
    // the tube frame's own centroid shift pushes its far-side ring band past 1.1x
    // its baseline, flagging the ring instead of the bulge.
    const CapsuleFit roughFrame = FitPositionsOnly(cloud, fitOptions);
    const CapsuleFit& rframe = roughFrame.ok ? roughFrame : base;
    if (getenv("CSS_COMPOUND_DEBUG"))
        std::printf("  [rough frame origin %.3f %.3f %.3f  axis %.3f %.3f %.3f  length %.3f]\n", rframe.origin.x, rframe.origin.y,
                   rframe.origin.z, rframe.axis.x, rframe.axis.y, rframe.axis.z, rframe.length);
    RegionPass rough = makeRegions(allIdx, rframe, 1.15f);
    if (rough.regions.empty()) {
        if (getenv("CSS_COMPOUND_DEBUG"))
            std::printf("  [no bulge regions]\n");
        return best;
    }
    std::vector<int> tubeIdx;
    tubeIdx.reserve(cloud.p.size());
    for (std::size_t i = 0; i < cloud.p.size(); ++i)
        if (!rough.excluded[i])
            tubeIdx.push_back((int)i);
    if (tubeIdx.size() < 200)
        return best;
    // the tube fit is positions-only + robust P80: the tube subset holds unflagged
    // bulge points (between the ring and the flag threshold, the top ~20% of the
    // contaminated bins); the ring is the dominant ~70%, so P80 lands in the ring
    // band while the parts keep the full percentile for bulge coverage
    FitOptions tubeOpts = fitOptions;
    tubeOpts.profilePercentile = std::min(fitOptions.profilePercentile, 0.8f);
    const CapsuleFit tubeFit = subCloudFit(tubeIdx, tubeOpts, true);
    if (!tubeFit.ok)
        return best;
    if (getenv("CSS_COMPOUND_DEBUG")) {
        std::printf("  [tube fit origin %.3f %.3f %.3f  axis %.3f %.3f %.3f  length %.3f  base score %.5f]\n", tubeFit.origin.x,
                   tubeFit.origin.y, tubeFit.origin.z, tubeFit.axis.x, tubeFit.axis.y, tubeFit.axis.z, tubeFit.length,
                   best.score);
        for (const auto& t : tubeFit.tapers)
            std::printf("  [tube taper s0 %.3f  s1 %.3f  r0 %.3f  r1 %.3f]\n", t.s0, t.s1, t.r0, t.r1);
        std::vector<std::pair<float, int>> tr;
        for (std::size_t i = 0; i < tubeIdx.size(); ++i) {
            const Vec3 d = cloud.p[tubeIdx[i]] - tubeFit.origin;
            const float s = Dot(d, tubeFit.axis);
            tr.emplace_back(Length(d - tubeFit.axis * s), tubeIdx[i]);
        }
        std::sort(tr.begin(), tr.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (int i = 0; i < 8 && i < (int)tr.size(); ++i) {
            const Vec3& p = cloud.p[tr[i].second];
            std::printf("  [tube hi r %.4f  pt %.3f %.3f %.3f]\n", tr[i].first, p.x, p.y, p.z);
        }
        std::printf("  [tube subset n %zu  P90 %.4f  P99 %.4f  max %.4f  excluded %zu]\n", tr.size(),
                   tr[(std::size_t)(0.9f * (tr.size() - 1))].first, tr[(std::size_t)(0.99f * (tr.size() - 1))].first,
                   tr[0].first, cloud.p.size() - tr.size());
    }
    std::sort(rough.regions.begin(), rough.regions.end(), [](const Region& a, const Region& b) {
        return a.shell.size() > b.shell.size();
    });
    std::vector<CapsuleFit> parts{tubeFit};
    std::vector<std::vector<int>> partIdx{tubeIdx};
    Score3 cur = scoreParts(parts, partIdx);
    if (getenv("CSS_COMPOUND_DEBUG"))
        std::printf("  [tube  overhang %.5f  underfit %.5f  score %.5f]\n", cur.overhang, cur.underfit, cur.score);
    for (const auto& rg : rough.regions) {
        // fit on the flagged shell (the bulge points only; the region's cells also
        // hold inner bulge points inside the tube, which wreck both fit models).
        // Two models: normal-aware (a sphere for blob-like shells, a chain
        // otherwise) and a chain constrained to the bone frame (a wide flat cap
        // like the calf degenerates its own PCA and normal center of curvature;
        // the bone-axis chain is the right model there); keep the lowest-error
        Cloud shellCloud;
        shellCloud.p.reserve(rg.shell.size());
        if (cloud.hasNormals)
            shellCloud.n.reserve(rg.shell.size());
        for (const int i : rg.shell) {
            shellCloud.p.push_back(cloud.p[i]);
            if (cloud.hasNormals)
                shellCloud.n.push_back(cloud.n[i]);
        }
        shellCloud.hasNormals = cloud.hasNormals;
        const CapsuleFit rfA = subCloudFit(rg.shell, fitOptions, false);
        const CapsuleFit rfC = FitToFrame(shellCloud, rframe.axis, fitOptions);
        const CapsuleFit* rf = nullptr;
        for (const CapsuleFit* f : {&rfA, &rfC})
            if (f->ok && (!rf || f->maxError < rf->maxError))
                rf = f;
        if (getenv("CSS_COMPOUND_DEBUG")) {
            const char* tag[2] = {"normal", "frame"};
            const CapsuleFit* fits[2] = {&rfA, &rfC};
            for (int i = 0; i < 2; ++i)
                std::printf("  [fit %s  ok %d  maxError %.4f  length %.3f  origin %.3f %.3f %.3f  axis %.3f %.3f %.3f]\n", tag[i],
                           fits[i]->ok ? 1 : 0, fits[i]->maxError, fits[i]->length, fits[i]->origin.x, fits[i]->origin.y, fits[i]->origin.z,
                           fits[i]->axis.x, fits[i]->axis.y, fits[i]->axis.z);
        }
        if (!rf) {
            if (getenv("CSS_COMPOUND_DEBUG"))
                std::printf("  [region %zu pts  fit failed]\n", rg.shell.size());
            continue;
        }
        // a part must actually fit its own shell: a shallow fragment can fit a
        // huge sphere (or a mis-centered one) that pokes far outside the skin, and
        // the overhang metric's support gate cannot see such a poke
        float partR = 0.f;
        for (const auto& t : rf->tapers)
            partR = std::max({partR, t.r0, t.r1});
        if (partR > 1e-9f && rf->maxError > 0.25f * partR) {
            if (getenv("CSS_COMPOUND_DEBUG"))
                std::printf("  [region %zu pts  rejected maxError %.4f > %.4f (R %.3f)]\n", rg.shell.size(), rf->maxError,
                           0.25f * partR, partR);
            continue;
        }
        std::vector<CapsuleFit> cand = parts;
        cand.push_back(*rf);
        std::vector<std::vector<int>> candIdx = partIdx;
        candIdx.push_back(rg.shell);
        const Score3 cs = scoreParts(cand, candIdx);
        if (getenv("CSS_COMPOUND_DEBUG")) {
            std::printf("  [region %zu pts  overhang %.5f  underfit %.5f  score %.5f  cur %.5f]\n", rg.shell.size(), cs.overhang,
                       cs.underfit, cs.score, cur.score);
            std::printf("  [region origin %.3f %.3f %.3f  axis %.3f %.3f %.3f  length %.3f]\n", rf->origin.x, rf->origin.y, rf->origin.z,
                       rf->axis.x, rf->axis.y, rf->axis.z, rf->length);
            for (const auto& t : rf->tapers)
                std::printf("  [region taper s0 %.3f  s1 %.3f  r0 %.3f  r1 %.3f]\n", t.s0, t.s1, t.r0, t.r1);
        }
        // a per-part penalty: a part must buy its shape cost, or the greedy
        // accumulates redundant capsules that cover a skin sliver (0.005 ~ 1.7mm of
        // underfit at underfitWeight 3)
        if (cs.score < cur.score - 0.005f) {
            parts = std::move(cand);
            partIdx = std::move(candIdx);
            cur = cs;
        }
    }
    if (cur.score < best.score) {
        best.parts = std::move(parts);
        best.underfit = cur.underfit;
        best.overhang = cur.overhang;
        best.score = cur.score;
    }
    return best;
}

} // namespace

float DistanceToShape(const CapsuleFit& fit, const Vec3& p) {
    return SignedDistanceToShape(fit, p);
}

CapsuleFit FitTaperedCapsule(const float* vertexData, const float* normals, std::size_t vertexCount, const FitOptions& fitOptions) {
    if (vertexData == nullptr || vertexCount < 3)
        return {};
    const Cloud cloud = ReadCloud(vertexData, normals, vertexCount);
    if (cloud.hasNormals)
        return FitNormalAware(cloud, fitOptions);
    return FitPositionsOnly(cloud, fitOptions);
}

CompoundFit FitTaperedCapsuleCompound(const float* vertexData, const float* normals, std::size_t vertexCount, const FitOptions& fitOptions) {
    if (vertexData == nullptr || vertexCount < 3)
        return {};
    const Cloud cloud = ReadCloud(vertexData, normals, vertexCount);
    return FitCloudCompound(cloud, fitOptions);
}

std::string FormatFitReport(const CapsuleFit& fit) {
    if (!fit.ok)
        return "  fit failed\n";
    std::string s;
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "  origin %8.3f %8.3f %8.3f  axis %8.3f %8.3f %8.3f  length %8.3f\n"
                  "  rms %8.4f  max %8.4f  mean %8.4f  profile %8.4f  overhang %8.4f  segments %d\n",
                  fit.origin.x, fit.origin.y, fit.origin.z,
                  fit.axis.x, fit.axis.y, fit.axis.z, fit.length,
                  fit.rmsError, fit.maxError, fit.meanSignedError, fit.profileScore, fit.overhangScore, fit.segments);
    s += buf;
    for (const Taper& t : fit.tapers) {
        std::snprintf(buf, sizeof buf, "    taper s0 %8.3f  s1 %8.3f  r0 %8.3f  r1 %8.3f\n", t.s0, t.s1, t.r0, t.r1);
        s += buf;
    }
    return s;
}

} // namespace css
