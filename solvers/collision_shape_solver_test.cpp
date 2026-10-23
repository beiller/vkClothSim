#include "collision_shape_solver.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>

using namespace css;

namespace {

int gFailures = 0;

void Check(bool pass, const char* name, float value, float limit) {
    if (!pass)
        ++gFailures;
    std::printf("  %-26s %s  value %9.4f  limit %9.4f\n", name, pass ? "PASS" : "FAIL", value, limit);
}

float AxisAngleDeg(Vec3 a, Vec3 b) {
    const float d = std::fabs(Dot(Normalize(a), Normalize(b)));
    return std::acos(std::min(1.0f, d)) * 57.2957795131f;
}

// the solver may return either sign of the axis; expected values mirror accordingly
bool AxisFlipped(const Vec3& fitAxis, const Vec3& gtAxis) { return Dot(fitAxis, gtAxis) < 0.f; }

void MakePerpendicularFrame(Vec3 axis, Vec3& outU, Vec3& outV) {
    const Vec3 ref = std::fabs(axis.z) < 0.9f ? Vec3{0.f, 0.f, 1.f} : Vec3{1.f, 0.f, 0.f};
    outU = Normalize(Cross(axis, ref));
    outV = Cross(axis, outU);
}

// samples the surface of revolution: p = axis * s + (u * cos + v * sin) * radiusOf(s)
std::vector<float> MakeMesh(const std::function<float(float)>& radiusOf, float sMin, float sMax, const Vec3& axis, int sSteps, int thetaSteps, float noise, std::mt19937& rnd) {
    Vec3 u, v;
    MakePerpendicularFrame(axis, u, v);
    std::vector<float> data;
    std::normal_distribution<float> noiseDist(0.f, noise);
    const auto jitter = [&]() { return Vec3{noiseDist(rnd), noiseDist(rnd), noiseDist(rnd)}; };
    for (int a = 0; a < sSteps; ++a) {
        const float s = sMin + (sMax - sMin) * (float)a / (float)(sSteps - 1);
        const float r = radiusOf(s);
        for (int b = 0; b < thetaSteps; ++b) {
            const float th = 6.28318530718f * (float)b / (float)thetaSteps;
            const Vec3 p = axis * s + u * (r * std::cos(th)) + v * (r * std::sin(th)) + jitter();
            data.push_back(p.x);
            data.push_back(p.y);
            data.push_back(p.z);
        }
    }
    return data;
}

struct MeshWithNormals {
    std::vector<float> pos;
    std::vector<float> nrm;
};

// samples the surface of revolution with the true outward normals (radial direction)
MeshWithNormals MakeMeshWithNormals(const std::function<float(float)>& radiusOf, float sMin, float sMax, const Vec3& axis, int sSteps, int thetaSteps, float noise, std::mt19937& rnd) {
    Vec3 u, v;
    MakePerpendicularFrame(axis, u, v);
    MeshWithNormals mesh;
    std::normal_distribution<float> noiseDist(0.f, noise);
    const auto jitter = [&]() { return Vec3{noiseDist(rnd), noiseDist(rnd), noiseDist(rnd)}; };
    for (int a = 0; a < sSteps; ++a) {
        const float s = sMin + (sMax - sMin) * (float)a / (float)(sSteps - 1);
        const float r = radiusOf(s);
        for (int b = 0; b < thetaSteps; ++b) {
            const float th = 6.28318530718f * (float)b / (float)thetaSteps;
            const Vec3 radial = u * std::cos(th) + v * std::sin(th);
            const Vec3 ideal = axis * s + radial * r;
            const Vec3 p = ideal + jitter();
            mesh.pos.push_back(p.x);
            mesh.pos.push_back(p.y);
            mesh.pos.push_back(p.z);
            const Vec3 n = Normalize(ideal - axis * s);
            mesh.nrm.push_back(n.x);
            mesh.nrm.push_back(n.y);
            mesh.nrm.push_back(n.z);
        }
    }
    return mesh;
}

// a spherical cap (polar angle < maxPhiDeg) around +z at the given center, with the true
// outward normals
MeshWithNormals MakeSphereCap(Vec3 center, float radius, float maxPhiDeg, int phiSteps, int thetaSteps, float noise, std::mt19937& rnd) {
    const Vec3 axis{0.f, 0.f, 1.f};
    Vec3 u, v;
    MakePerpendicularFrame(axis, u, v);
    MeshWithNormals mesh;
    std::normal_distribution<float> noiseDist(0.f, noise);
    const auto jitter = [&]() { return Vec3{noiseDist(rnd), noiseDist(rnd), noiseDist(rnd)}; };
    const float deg2rad = 3.14159265f / 180.f;
    for (int a = 0; a < phiSteps; ++a) {
        const float phi = deg2rad * maxPhiDeg * (float)a / (float)(phiSteps - 1);
        const float r = radius * std::sin(phi);
        const float h = radius * std::cos(phi);
        for (int b = 0; b < thetaSteps; ++b) {
            const float th = 6.28318530718f * (float)b / (float)thetaSteps;
            const Vec3 radial = u * std::cos(th) + v * std::sin(th);
            const Vec3 ideal = center + axis * h + radial * r;
            const Vec3 p = ideal + jitter();
            mesh.pos.push_back(p.x);
            mesh.pos.push_back(p.y);
            mesh.pos.push_back(p.z);
            const Vec3 n = Normalize(ideal - center);
            mesh.nrm.push_back(n.x);
            mesh.nrm.push_back(n.y);
            mesh.nrm.push_back(n.z);
        }
    }
    return mesh;
}

std::function<float(float)> TaperedCapsuleProfile(float r0, float r1, float cylinderLength) {
    return [r0, r1, cylinderLength](float s) {
        if (s < 0.f)
            return std::sqrt(std::max(0.f, r0 * r0 - s * s));
        if (s > cylinderLength)
            return std::sqrt(std::max(0.f, r1 * r1 - (s - cylinderLength) * (s - cylinderLength)));
        return r0 + (r1 - r0) * s / cylinderLength;
    };
}

void RandomAxis(std::mt19937& rnd, Vec3& outAxis) {
    std::normal_distribution<float> g(0.f, 1.f);
    do {
        outAxis = Vec3{g(rnd), g(rnd), g(rnd)};
    } while (Length(outAxis) < 0.1f);
    outAxis = Normalize(outAxis);
}

float MaxProfileRadius(const CapsuleFit& fit) {
    float mx = 0.f;
    for (const Taper& t : fit.tapers)
        mx = std::max(mx, std::max(t.r0, t.r1));
    return mx;
}

void RunFrustumTest() {
    std::printf("frustum (tapered cylinder) r0=0.6 r1=1.4 length=3.0 noise=0.02\n");
    std::mt19937 rnd(42);
    Vec3 axis;
    RandomAxis(rnd, axis);
    const float r0 = 0.6f;
    const float r1 = 1.4f;
    const float length = 3.0f;
    const std::vector<float> data = MakeMesh([r0, r1, length](float s) { return r0 + (r1 - r0) * s / length; }, 0.f, length, axis, 200, 36, 0.02f, rnd);
    const CapsuleFit fit = FitTaperedCapsule(data.data(), data.size() / 3);
    std::printf("%s", FormatFitReport(fit).c_str());
    const bool flipped = AxisFlipped(fit.axis, axis);
    const float expR0 = flipped ? r1 : r0;
    const float expR1 = flipped ? r0 : r1;
    const Vec3 expectedOrigin = flipped ? axis * length : Vec3{0.f, 0.f, 0.f};
    Check(fit.ok, "fit ok", (float)fit.ok, 1.0f);
    Check(AxisAngleDeg(fit.axis, axis) < 2.0f, "axis angle deg", AxisAngleDeg(fit.axis, axis), 2.0f);
    Check(std::fabs(fit.length - length) / length < 0.08f, "length rel err", std::fabs(fit.length - length) / length, 0.08f);
    Check(std::fabs(fit.tapers.front().r0 - expR0) / expR0 < 0.12f, "r0 rel err", std::fabs(fit.tapers.front().r0 - expR0) / expR0, 0.12f);
    Check(std::fabs(fit.tapers.back().r1 - expR1) / expR1 < 0.12f, "r1 rel err", std::fabs(fit.tapers.back().r1 - expR1) / expR1, 0.12f);
    Check(Length(fit.origin - expectedOrigin) < 0.2f, "origin dist", Length(fit.origin - expectedOrigin), 0.2f);
    Check(fit.rmsError < 0.07f, "rms error", fit.rmsError, 0.07f);
    Check(fit.segments == 1, "single segment", (float)fit.segments, 1.0f);
}

void RunCapsuleTest() {
    std::printf("capsule r=1.0 cylinder=3.0 (full extent 5.0) noise=0.02\n");
    std::mt19937 rnd(7);
    Vec3 axis;
    RandomAxis(rnd, axis);
    const float r = 1.0f;
    const std::vector<float> data = MakeMesh(TaperedCapsuleProfile(r, r, 3.0f), -r, 3.0f + r, axis, 200, 36, 0.02f, rnd);
    const CapsuleFit fit = FitTaperedCapsule(data.data(), data.size() / 3);
    std::printf("%s", FormatFitReport(fit).c_str());
    const bool flipped = AxisFlipped(fit.axis, axis);
    const float fullExtent = 3.0f + 2.0f * r;
    const Vec3 expectedOrigin = flipped ? axis * (3.0f + r) : axis * -r;
    Check(fit.ok, "fit ok", (float)fit.ok, 1.0f);
    Check(AxisAngleDeg(fit.axis, axis) < 2.0f, "axis angle deg", AxisAngleDeg(fit.axis, axis), 2.0f);
    Check(std::fabs(fit.length - fullExtent) / fullExtent < 0.08f, "length rel err", std::fabs(fit.length - fullExtent) / fullExtent, 0.08f);
    Check(Length(fit.origin - expectedOrigin) < 0.25f, "origin dist", Length(fit.origin - expectedOrigin), 0.25f);
    Check(std::fabs(MaxProfileRadius(fit) - r) / r < 0.12f, "max radius rel err", std::fabs(MaxProfileRadius(fit) - r) / r, 0.12f);
    Check(fit.rmsError < 0.15f, "rms error", fit.rmsError, 0.15f);
    Check(fit.segments >= 2, "multi segment", (float)fit.segments, 2.0f);
}

void RunTaperedCapsuleTest() {
    std::printf("tapered capsule r0=0.7 r1=1.3 cylinder=3.0 noise=0.02\n");
    std::mt19937 rnd(13);
    Vec3 axis;
    RandomAxis(rnd, axis);
    const float r0 = 0.7f;
    const float r1 = 1.3f;
    const std::vector<float> data = MakeMesh(TaperedCapsuleProfile(r0, r1, 3.0f), -r0, 3.0f + r1, axis, 200, 36, 0.02f, rnd);
    const CapsuleFit fit = FitTaperedCapsule(data.data(), data.size() / 3);
    std::printf("%s", FormatFitReport(fit).c_str());
    const bool flipped = AxisFlipped(fit.axis, axis);
    const float fullExtent = 3.0f + r0 + r1;
    const Vec3 expectedOrigin = flipped ? axis * (3.0f + r1) : axis * -r0;
    Check(fit.ok, "fit ok", (float)fit.ok, 1.0f);
    Check(AxisAngleDeg(fit.axis, axis) < 2.0f, "axis angle deg", AxisAngleDeg(fit.axis, axis), 2.0f);
    Check(std::fabs(fit.length - fullExtent) / fullExtent < 0.08f, "length rel err", std::fabs(fit.length - fullExtent) / fullExtent, 0.08f);
    Check(Length(fit.origin - expectedOrigin) < 0.25f, "origin dist", Length(fit.origin - expectedOrigin), 0.25f);
    Check(std::fabs(MaxProfileRadius(fit) - r1) / r1 < 0.12f, "max radius rel err", std::fabs(MaxProfileRadius(fit) - r1) / r1, 0.12f);
    Check(fit.rmsError < 0.2f, "rms error", fit.rmsError, 0.2f);
}

void RunNoisyFrustumTest() {
    std::printf("noisy frustum r0=0.6 r1=1.4 length=3.0 noise=0.08\n");
    std::mt19937 rnd(99);
    Vec3 axis;
    RandomAxis(rnd, axis);
    const float r0 = 0.6f;
    const float r1 = 1.4f;
    const float length = 3.0f;
    const std::vector<float> data = MakeMesh([r0, r1, length](float s) { return r0 + (r1 - r0) * s / length; }, 0.f, length, axis, 200, 36, 0.08f, rnd);
    const CapsuleFit fit = FitTaperedCapsule(data.data(), data.size() / 3);
    std::printf("%s", FormatFitReport(fit).c_str());
    Check(fit.ok, "fit ok", (float)fit.ok, 1.0f);
    Check(AxisAngleDeg(fit.axis, axis) < 3.0f, "axis angle deg", AxisAngleDeg(fit.axis, axis), 3.0f);
    Check(std::fabs(fit.length - length) / length < 0.18f, "length rel err", std::fabs(fit.length - length) / length, 0.18f);
    Check(fit.rmsError < 0.15f, "rms error", fit.rmsError, 0.15f);
    Check(std::fabs(fit.profileScore) >= 0.f, "profile score finite", fit.profileScore, 1e30f);
}

void RunDegenerateTests() {
    std::printf("degenerate inputs\n");
    std::vector<float> constant(300, 1.f);
    for (size_t i = 1; i < constant.size(); i += 3)
        constant[i] = 2.f;
    const CapsuleFit flat = FitTaperedCapsule(constant.data(), constant.size() / 3);
    Check(!flat.ok, "constant points rejected", (float)flat.ok, 0.0f);
    const float two[6] = {0, 0, 0, 1, 0, 0};
    const CapsuleFit twoPts = FitTaperedCapsule(two, 2);
    Check(!twoPts.ok, "two points rejected", (float)twoPts.ok, 0.0f);
    const CapsuleFit nil = FitTaperedCapsule(nullptr, 10);
    Check(!nil.ok, "null input rejected", (float)nil.ok, 0.0f);
}

void RunFrustumNormalsTest() {
    std::printf("frustum (tapered cylinder) r0=0.6 r1=1.4 length=3.0 noise=0.02 [with normals]\n");
    std::mt19937 rnd(42);
    Vec3 axis;
    RandomAxis(rnd, axis);
    const float r0 = 0.6f;
    const float r1 = 1.4f;
    const float length = 3.0f;
    const MeshWithNormals mesh = MakeMeshWithNormals([r0, r1, length](float s) { return r0 + (r1 - r0) * s / length; }, 0.f, length, axis, 200, 36, 0.02f, rnd);
    const CapsuleFit fit = FitTaperedCapsule(mesh.pos.data(), mesh.nrm.data(), mesh.pos.size() / 3);
    std::printf("%s", FormatFitReport(fit).c_str());
    const bool flipped = AxisFlipped(fit.axis, axis);
    const float expR0 = flipped ? r1 : r0;
    const float expR1 = flipped ? r0 : r1;
    const Vec3 expectedOrigin = flipped ? axis * length : Vec3{0.f, 0.f, 0.f};
    Check(fit.ok, "fit ok", (float)fit.ok, 1.0f);
    Check(AxisAngleDeg(fit.axis, axis) < 2.0f, "axis angle deg", AxisAngleDeg(fit.axis, axis), 2.0f);
    Check(std::fabs(fit.length - length) / length < 0.08f, "length rel err", std::fabs(fit.length - length) / length, 0.08f);
    Check(std::fabs(fit.tapers.front().r0 - expR0) / expR0 < 0.12f, "r0 rel err", std::fabs(fit.tapers.front().r0 - expR0) / expR0, 0.12f);
    Check(std::fabs(fit.tapers.back().r1 - expR1) / expR1 < 0.12f, "r1 rel err", std::fabs(fit.tapers.back().r1 - expR1) / expR1, 0.12f);
    Check(Length(fit.origin - expectedOrigin) < 0.2f, "origin dist", Length(fit.origin - expectedOrigin), 0.2f);
    Check(fit.rmsError < 0.07f, "rms error", fit.rmsError, 0.07f);
    Check(fit.segments == 1, "single segment", (float)fit.segments, 1.0f);
}

void RunTaperedCapsuleNormalsTest() {
    std::printf("tapered capsule r0=0.7 r1=1.3 cylinder=3.0 noise=0.02 [with normals]\n");
    std::mt19937 rnd(13);
    Vec3 axis;
    RandomAxis(rnd, axis);
    const float r0 = 0.7f;
    const float r1 = 1.3f;
    const MeshWithNormals mesh = MakeMeshWithNormals(TaperedCapsuleProfile(r0, r1, 3.0f), -r0, 3.0f + r1, axis, 200, 36, 0.02f, rnd);
    const CapsuleFit fit = FitTaperedCapsule(mesh.pos.data(), mesh.nrm.data(), mesh.pos.size() / 3);
    std::printf("%s", FormatFitReport(fit).c_str());
    const bool flipped = AxisFlipped(fit.axis, axis);
    const float fullExtent = 3.0f + r0 + r1;
    const Vec3 expectedOrigin = flipped ? axis * (3.0f + r1) : axis * -r0;
    Check(fit.ok, "fit ok", (float)fit.ok, 1.0f);
    Check(AxisAngleDeg(fit.axis, axis) < 2.0f, "axis angle deg", AxisAngleDeg(fit.axis, axis), 2.0f);
    Check(std::fabs(fit.length - fullExtent) / fullExtent < 0.08f, "length rel err", std::fabs(fit.length - fullExtent) / fullExtent, 0.08f);
    Check(Length(fit.origin - expectedOrigin) < 0.25f, "origin dist", Length(fit.origin - expectedOrigin), 0.25f);
    Check(std::fabs(MaxProfileRadius(fit) - r1) / r1 < 0.12f, "max radius rel err", std::fabs(MaxProfileRadius(fit) - r1) / r1, 0.12f);
    Check(fit.rmsError < 0.2f, "rms error", fit.rmsError, 0.2f);
}

void RunCylinderBlobNormalsTest() {
    std::printf("short cylinder (blob) r=1.0 length=2.2 noise=0.02 [normal-aware vs chord sphere]\n");
    std::mt19937 rnd(21);
    const float r = 1.0f;
    const float length = 2.2f;
    // blob-like (aspect 2.2 < 2.5): the chord sphere overestimates (its radius spans the
    // chord through the hollow); the normal-aware sphere uses the inward depth along the
    // normals, which is the true radius of the shell
    const MeshWithNormals mesh = MakeMeshWithNormals([r](float) { return r; }, -length / 2.f, length / 2.f, Vec3{0.f, 0.f, 1.f}, 80, 72, 0.02f, rnd);
    const CapsuleFit fit = FitTaperedCapsule(mesh.pos.data(), mesh.nrm.data(), mesh.pos.size() / 3);
    std::printf("%s", FormatFitReport(fit).c_str());
    Check(fit.ok, "fit ok", (float)fit.ok, 1.0f);
    Check(fit.length <= 1e-9f, "sphere (length 0)", fit.length, 1e-9f);
    Check(std::fabs(MaxProfileRadius(fit) - r) / r < 0.05f, "radius rel err", std::fabs(MaxProfileRadius(fit) - r) / r, 0.05f);
    Check(Length(fit.origin) < 0.1f, "center dist", Length(fit.origin), 0.1f);
    const CapsuleFit chordFit = FitTaperedCapsule(mesh.pos.data(), mesh.pos.size() / 3);
    std::printf("%s", FormatFitReport(chordFit).c_str());
    Check(chordFit.ok && MaxProfileRadius(chordFit) > 1.15f, "chord overestimates (sanity)", MaxProfileRadius(chordFit), 1.15f);
}

void RunSphereCapNormalsTest() {
    std::printf("spherical cap r=1.0 phi<55deg noise=0.03 [normal-aware sphere]\n");
    std::mt19937 rnd(33);
    const Vec3 center{0.5f, -0.3f, 0.2f};
    const float radius = 1.0f;
    const MeshWithNormals mesh = MakeSphereCap(center, radius, 55.f, 40, 60, 0.03f, rnd);
    const CapsuleFit fit = FitTaperedCapsule(mesh.pos.data(), mesh.nrm.data(), mesh.pos.size() / 3);
    std::printf("%s", FormatFitReport(fit).c_str());
    Check(fit.ok, "fit ok", (float)fit.ok, 1.0f);
    Check(fit.length <= 1e-9f, "sphere (length 0)", fit.length, 1e-9f);
    Check(Length(fit.origin - center) < 0.1f, "center dist", Length(fit.origin - center), 0.1f);
    Check(std::fabs(MaxProfileRadius(fit) - radius) / radius < 0.05f, "radius rel err", std::fabs(MaxProfileRadius(fit) - radius) / radius, 0.05f);
}

void RunCompoundBulgeTest() {
    std::printf("cylinder r=1.0 length=3.0 + fused side bulge r=0.8 [compound vs single]\n");
    std::mt19937 rnd(55);
    const float r = 1.0f;
    const float length = 3.0f;
    const Vec3 axis{0.f, 0.f, 1.f};
    Vec3 u, v;
    MakePerpendicularFrame(axis, u, v);
    std::vector<float> pos, nrm;
    const MeshWithNormals tube = MakeMeshWithNormals([r, length](float) { return r; }, -length / 2.f, length / 2.f, axis, 120, 48, 0.02f, rnd);
    pos.insert(pos.end(), tube.pos.begin(), tube.pos.end());
    nrm.insert(nrm.end(), tube.nrm.begin(), tube.nrm.end());
    // the "calf": a sphere r=0.8 fused on the +u side, centered 1.1 along u (tip at 1.9)
    const Vec3 bulgeCenter = u * 1.1f;
    const float bulgeR = 0.8f;
    std::normal_distribution<float> noiseDist(0.f, 0.02f);
    for (int a = 1; a < 24; ++a) {
        const float phi = 3.14159265f * (float)a / 24.f;
        const float ring = bulgeR * std::sin(phi);
        const float h = bulgeR * std::cos(phi);
        for (int b = 0; b < 48; ++b) {
            const float th = 6.28318530718f * (float)b / 48.f;
            const Vec3 radial = u * std::cos(th) + v * std::sin(th);
            const Vec3 ideal = bulgeCenter + axis * h + radial * ring;
            const Vec3 p = ideal + Vec3{noiseDist(rnd), noiseDist(rnd), noiseDist(rnd)};
            pos.push_back(p.x);
            pos.push_back(p.y);
            pos.push_back(p.z);
            const Vec3 n = Normalize(ideal - bulgeCenter);
            nrm.push_back(n.x);
            nrm.push_back(n.y);
            nrm.push_back(n.z);
        }
    }
    FitOptions opts;
    opts.profilePercentile = 1.0f; // per-bin max, as the probe uses
    const CompoundFit compound = FitTaperedCapsuleCompound(pos.data(), nrm.data(), pos.size() / 3, opts);
    const CapsuleFit single = FitTaperedCapsule(pos.data(), nrm.data(), pos.size() / 3, opts);
    const auto unionSdf = [](const CompoundFit& c, Vec3 p) {
        float d = 1e30f;
        for (const CapsuleFit& part : c.parts)
            d = std::min(d, DistanceToShape(part, p));
        return d;
    };
    const Vec3 farSide = u * (-(r + 0.4f)); // 0.4 beyond the tube skin, on the bulge-free side
    const Vec3 bulgeTip = u * (1.1f + bulgeR);
    std::printf("  compound: %zu parts, underfit %.4f, overhang %.4f\n", compound.parts.size(), compound.underfit, compound.overhang);
    std::printf("%s", FormatFitReport(single).c_str());
    Check(compound.ok, "compound ok", (float)compound.ok, 1.0f);
    Check(compound.parts.size() >= 2, "split into parts", (float)compound.parts.size(), 2.0f);
    Check(compound.underfit < 0.1f, "bulge covered (underfit)", compound.underfit, 0.1f);
    Check(unionSdf(compound, farSide) > 0.1f, "far side not over-covered", unionSdf(compound, farSide), 0.1f);
    Check(unionSdf(compound, bulgeTip) < 0.05f, "bulge tip inside", unionSdf(compound, bulgeTip), 0.05f);
    Check(DistanceToShape(single, farSide) < 0.f, "single fit over-covers (sanity)", DistanceToShape(single, farSide), 0.0f);
}

} // namespace

int main() {
    std::printf("collision shape solver tests\n");
    RunFrustumTest();
    RunCapsuleTest();
    RunTaperedCapsuleTest();
    RunNoisyFrustumTest();
    RunDegenerateTests();
    RunFrustumNormalsTest();
    RunTaperedCapsuleNormalsTest();
    RunCylinderBlobNormalsTest();
    RunSphereCapNormalsTest();
    RunCompoundBulgeTest();
    std::printf("%s\n", gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return gFailures == 0 ? 0 : 1;
}
