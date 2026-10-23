#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace css {

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
inline Vec3 Normalize(Vec3 a) {
    const float l = Length(a);
    return l > 1e-12f ? a * (1.f / l) : Vec3{0.f, 0.f, 1.f};
}

// one piece of a (possibly chained) tapered capsule: radius ramps linearly from r0 at s0 to r1 at s1
struct Taper {
    float s0 = 0.f;
    float s1 = 0.f;
    float r0 = 0.f;
    float r1 = 0.f;
};

struct FitOptions {
    int profileBins = 32;
    float profilePercentile = 0.9f; // robust against interior / noisy vertices
    int profileDilate = 1;          // sliding-max window: fills single-bin holes where the
                                   // surface blended onto an adjacent bone (e.g. the calf bulge)
    float underfitWeight = 3.f;     // a shape that is too small is weighted worse than one that is too big
    float overfitWeight = 1.f;
    float tolerance = 0.1f;         // fraction of mean profile radius; first segment count meeting it wins
    int maxSegments = 4;
    float overhangWeight = 1.f;     // weight of the shape-surface-to-cloud term (same units: length)
    float overhangPercentile = 0.95f;
    float sphereAspect = 2.5f;     // axial extent / mean radius below this = blob-like: fit a sphere, not a capsule
    float spherePercentile = 0.9f; // sphere radius percentile (robust; independent of profilePercentile)
    int maxClusters = 4;           // compound fit: up to this many 3D clusters (one capsule chain each)
};

struct CapsuleFit {
    bool ok = false;
    Vec3 origin;                  // axis start point (profile s = 0)
    Vec3 axis = {0.f, 0.f, 1.f}; // principal axis (PCA), unit length
    float axisEigRatio = 1e30f;  // lambda1/lambda2; near 1 = blob-like cloud, axis unreliable
    float length = 0.f;          // full extent along the axis
    std::vector<Taper> tapers;   // one = single tapered capsule; more = chain
    int segments = 0;
    float rmsError = 0.f;        // RMS signed distance to the fitted shape (main error score)
    float maxError = 0.f;
    float meanSignedError = 0.f; // positive = shape overall too big
    float profileScore = 0.f;    // weighted profile RMS used for model selection
    float overhangScore = 0.f;   // P95 shape-surface-to-nearest-vertex distance (bulge penalty)
};

// a union of multiple tapered-capsule chains: one chain per 3D cluster of the cloud,
// for cross-sections asymmetric about any single axis (e.g. the calf bulge). m = 1
// degenerates to the plain single-cloud fit; the best cluster count is selected on the
// P95 uncovered skin plus the per-part overhang
struct CompoundFit {
    std::vector<CapsuleFit> parts;
    float underfit = 0.f; // P95 uncovered skin (positive part of the union SDF)
    float overhang = 0.f; // sum of the per-part overhang scores
    float score = 0.f;
    bool ok = false;
};

// distance from a point to the tapered chain (positive = outside the shape)
float DistanceToShape(const CapsuleFit& fit, const Vec3& p);

// Fits a (possibly chained) tapered capsule to vertex data.
// vertexData: vertexCount * 3 floats, xyz interleaved.
// normals: same layout, outward unit normals; null = fit from positions only.
// With normals the fit is normal-aware: the axis follows the body centerline (each
// point offset inward along its own normal) and a blob-like cloud becomes a sphere
// centered at the normal center of curvature, radius = P90 inward depth along the
// normals (true radial distance, not a chord).
CapsuleFit FitTaperedCapsule(const float* vertexData, const float* normals, std::size_t vertexCount, const FitOptions& fitOptions = {});

// Fits the union of up to fitOptions.maxClusters tapered-capsule chains to vertex data
// (same layout and normal semantics as FitTaperedCapsule)
CompoundFit FitTaperedCapsuleCompound(const float* vertexData, const float* normals, std::size_t vertexCount, const FitOptions& fitOptions = {});

// positions-only convenience
inline CapsuleFit FitTaperedCapsule(const float* vertexData, std::size_t vertexCount, const FitOptions& fitOptions = {}) {
    return FitTaperedCapsule(vertexData, nullptr, vertexCount, fitOptions);
}

std::string FormatFitReport(const CapsuleFit& fit);

} // namespace css
