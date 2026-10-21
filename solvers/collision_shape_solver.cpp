#include "collision_shape_solver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace css {
namespace {

using Mat3 = std::array<std::array<float, 3>, 3>;

std::vector<Vec3> ReadVertices(const float* vertexData, std::size_t vertexCount) {
    std::vector<Vec3> verts;
    verts.reserve(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i)
        verts.push_back({vertexData[i * 3 + 0], vertexData[i * 3 + 1], vertexData[i * 3 + 2]});
    return verts;
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

Vec3 ComputePrincipalAxis(const std::vector<Vec3>& verts) {
    const Vec3 centroid = ComputeCentroid(verts);
    const Mat3 covariance = ComputeCovariance(verts, centroid);
    std::array<float, 3> values{};
    Mat3 vectors{};
    SolveEigens3(covariance, values, vectors);
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

AxisProjection ProjectToAxis(const std::vector<Vec3>& verts, const Vec3& axis) {
    float minS = 1e30f;
    float maxS = -1e30f;
    for (const Vec3& v : verts) {
        const float s = Dot(v, axis);
        minS = std::min(minS, s);
        maxS = std::max(maxS, s);
    }
    return {axis, axis * minS, minS, maxS};
}

float PercentileValue(std::vector<float>& values, float percentile) {
    const std::size_t idx = (std::size_t)std::min((double)values.size() - 1.0, std::ceil(percentile * (double)values.size()) - 1.0);
    std::nth_element(values.begin(), values.begin() + (std::ptrdiff_t)idx, values.end());
    return values[idx];
}

std::vector<float> BuildRadiusProfile(const std::vector<Vec3>& verts, const AxisProjection& projection, int binCount, float percentile) {
    const float span = projection.sMax - projection.sMin;
    if (span <= 1e-6f)
        return {};
    const Vec3& axis = projection.axis;
    std::vector<std::vector<float>> binRadial(binCount);
    for (const Vec3& v : verts) {
        const float s = Dot(v, axis);
        const int bin = std::clamp((int)((s - projection.sMin) / span * (float)binCount), 0, binCount - 1);
        binRadial[bin].push_back(Length(v - axis * s));
    }
    std::vector<float> profile(binCount, -1.f);
    for (int i = 0; i < binCount; ++i)
        if (!binRadial[i].empty())
            profile[i] = PercentileValue(binRadial[i], percentile);
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

} // namespace

CapsuleFit FitTaperedCapsule(const float* vertexData, std::size_t vertexCount, const FitOptions& fitOptions) {
    CapsuleFit fit;
    if (vertexData == nullptr || vertexCount < 3)
        return fit;
    const std::vector<Vec3> verts = ReadVertices(vertexData, vertexCount);
    const Vec3 axis = ComputePrincipalAxis(verts);
    const AxisProjection projection = ProjectToAxis(verts, axis);
    const float length = projection.sMax - projection.sMin;
    if (length <= 1e-6f)
        return fit;
    fit.origin = projection.origin;
    fit.axis = axis;
    fit.length = length;
    const std::vector<float> profile = BuildRadiusProfile(verts, projection, fitOptions.profileBins, fitOptions.profilePercentile);
    if (profile.size() < 2)
        return fit;
    float meanRadius = 0.f;
    for (const float r : profile)
        meanRadius += r;
    meanRadius /= (float)profile.size();
    if (meanRadius <= 1e-9f)
        return fit;
    std::vector<Taper> bestTapers;
    float bestScore = 1e30f;
    for (int k = 1; k <= std::min(fitOptions.maxSegments, (int)profile.size()); ++k) {
        const std::vector<Taper> chain = FitChain(profile, k, length, fitOptions);
        if (chain.empty())
            continue;
        const float score = ComputeProfileScore(chain, profile, length, fitOptions);
        if (score < bestScore) {
            bestScore = score;
            bestTapers = chain;
        }
        if (score <= fitOptions.tolerance * meanRadius)
            break;
    }
    if (bestTapers.empty())
        return fit;
    fit.tapers = bestTapers;
    fit.segments = (int)bestTapers.size();
    fit.profileScore = bestScore;
    ComputeShapeError(fit, verts);
    fit.ok = true;
    return fit;
}

std::string FormatFitReport(const CapsuleFit& fit) {
    if (!fit.ok)
        return "  fit failed\n";
    std::string s;
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "  origin %8.3f %8.3f %8.3f  axis %8.3f %8.3f %8.3f  length %8.3f\n"
                  "  rms %8.4f  max %8.4f  mean %8.4f  profileScore %8.4f  segments %d\n",
                  fit.origin.x, fit.origin.y, fit.origin.z,
                  fit.axis.x, fit.axis.y, fit.axis.z, fit.length,
                  fit.rmsError, fit.maxError, fit.meanSignedError, fit.profileScore, fit.segments);
    s += buf;
    for (const Taper& t : fit.tapers) {
        std::snprintf(buf, sizeof buf, "    taper s0 %8.3f  s1 %8.3f  r0 %8.3f  r1 %8.3f\n", t.s0, t.s1, t.r0, t.r1);
        s += buf;
    }
    return s;
}

} // namespace css
