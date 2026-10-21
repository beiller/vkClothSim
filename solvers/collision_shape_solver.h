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
    float underfitWeight = 3.f;     // a shape that is too small is weighted worse than one that is too big
    float overfitWeight = 1.f;
    float tolerance = 0.1f;         // fraction of mean profile radius; first segment count meeting it wins
    int maxSegments = 4;
};

struct CapsuleFit {
    bool ok = false;
    Vec3 origin;                  // axis start point (profile s = 0)
    Vec3 axis = {0.f, 0.f, 1.f}; // principal axis (PCA), unit length
    float length = 0.f;          // full extent along the axis
    std::vector<Taper> tapers;   // one = single tapered capsule; more = chain
    int segments = 0;
    float rmsError = 0.f;        // RMS signed distance to the fitted shape (main error score)
    float maxError = 0.f;
    float meanSignedError = 0.f; // positive = shape overall too big
    float profileScore = 0.f;    // weighted profile RMS used for model selection
};

// Fits a (possibly chained) tapered capsule to vertex data.
// vertexData: vertexCount * 3 floats, xyz interleaved.
CapsuleFit FitTaperedCapsule(const float* vertexData, std::size_t vertexCount, const FitOptions& fitOptions = {});

std::string FormatFitReport(const CapsuleFit& fit);

} // namespace css
