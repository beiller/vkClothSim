// math.hpp
// Minimal column-major mat4 / vec3 (the camera math + the UBO packing). Header-only,
// no deps. (The Vulkan path does not use raylib/raymath.)
#pragma once
#include <cmath>

struct V3 { float x, y, z; };

inline V3 vSub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float vDot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 vCross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 vNorm(V3 a) { float l = std::sqrt(vDot(a, a)); return {a.x / l, a.y / l, a.z / l}; }

// Column-major 4x4 (m[col*4+row]); matches the GLSL mat4 layout exactly.
struct Mat4 { float m[16]; };

inline Mat4 mul4(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int rw = 0; rw < 4; ++rw) {
            float s = 0;
            for (int k = 0; k < 4; ++k)
                s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}

inline Mat4 perspective(float fovyDeg, float aspect, float nearP, float farP) {
    Mat4 r{};
    float f = 1.0f / std::tan(fovyDeg * 0.5f * 3.14159265f / 180.0f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (farP + nearP) / (nearP - farP);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * farP * nearP / (nearP - farP);
    return r;
}

inline Mat4 lookAt(V3 eye, V3 at, V3 up) {
    V3 f = vNorm(vSub(at, eye));
    V3 s = vNorm(vCross(f, up));
    V3 u = vCross(s, f);
    Mat4 r{};
    r.m[0] = s.x; r.m[1] = u.x; r.m[2] = -f.x; r.m[3] = 0;
    r.m[4] = s.y; r.m[5] = u.y; r.m[6] = -f.y; r.m[7] = 0;
    r.m[8] = s.z; r.m[9] = u.z; r.m[10] = -f.z; r.m[11] = 0;
    r.m[12] = -vDot(s, eye); r.m[13] = -vDot(u, eye); r.m[14] = vDot(f, eye); r.m[15] = 1;
    return r;
}
