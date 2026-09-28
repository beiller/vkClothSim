#pragma once
#include <cmath>
#include <numbers>

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kSqrt2 = std::numbers::sqrt2_v<float>;
inline constexpr float kFrameDt = 1.0f / 60.0f;

struct V3 {
    float x, y, z;
};

struct PointLight {
    V3 pos{0.0f, 3.0f, 0.0f};
    V3 color{1.0f, 0.97f, 0.92f};
    float intensity = 25.0f;
    float radius = 0.25f;
    float on = 1.0f;
    float shadowNear = 0.01f;
    float shadowFar = 30.0f;
    float shadowNormalBias = 0.05f;
    float shadowBiasBase = 0.3f;
    float shadowBiasSlope = 0.3f;
    float shadowSearchScale = 2.0f;
    float shadowMaxRadius = 16.0f;
};

struct V4 {
    float x, y, z, w;
};

inline V3 vAdd(V3 a, V3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline V3 vSub(V3 a, V3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline V3 vScale(V3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline float vDot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float vLen(V3 a) {
    return std::sqrt(vDot(a, a));
}
inline V3 vNorm(V3 a) {
    float l = vLen(a);
    return vScale(a, 1.0f / l);
}
inline V4 quatAxisAngle(V3 axis, float angle) {
    V3 a = vNorm(axis);
    const float s = std::sin(0.5f * angle);
    return {a.x * s, a.y * s, a.z * s, std::cos(0.5f * angle)};
}
inline V4 quatMul(V4 a, V4 b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline V3 vAt(const float* p, int i) {
    const int o = 3 * i;
    return {p[o], p[o + 1], p[o + 2]};
}
inline void vStore(float* p, int i, V3 v) {
    const int o = 3 * i;
    p[o] = v.x;
    p[o + 1] = v.y;
    p[o + 2] = v.z;
}
struct Mat4 {
    float m[16];
};

inline Mat4 mat4Identity() {
    Mat4 r{};
    r.m[0] = 1.0f;
    r.m[5] = 1.0f;
    r.m[10] = 1.0f;
    r.m[15] = 1.0f;
    return r;
}

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
    float f = 1.0f / std::tan(fovyDeg * 0.5f * kPi / 180.0f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (farP + nearP) / (nearP - farP);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * farP * nearP / (nearP - farP);
    return r;
}

inline void quatToMat3(const float q[4], float m[9]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1.0f - 2.0f * (y * y + z * z);
    m[1] = 2.0f * (x * y + z * w);
    m[2] = 2.0f * (x * z - y * w);
    m[3] = 2.0f * (x * y - z * w);
    m[4] = 1.0f - 2.0f * (x * x + z * z);
    m[5] = 2.0f * (y * z + x * w);
    m[6] = 2.0f * (x * z + y * w);
    m[7] = 2.0f * (y * z - x * w);
    m[8] = 1.0f - 2.0f * (x * x + y * y);
}
