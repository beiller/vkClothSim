#pragma once
#include <cmath>
#include <numbers>

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kSqrt2 = std::numbers::sqrt2_v<float>;
inline constexpr float kFrameDt = 1.0f / 60.0f;

struct V3 {
    float x, y, z;
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
inline V3 vCross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float vLen(V3 a) {
    return std::sqrt(vDot(a, a));
}
inline V3 vNorm(V3 a) {
    float l = vLen(a);
    return vScale(a, 1.0f / l);
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
inline V3 m3v(const float m[9], V3 v) {
    return {m[0] * v.x + m[3] * v.y + m[6] * v.z, m[1] * v.x + m[4] * v.y + m[7] * v.z,
            m[2] * v.x + m[5] * v.y + m[8] * v.z};
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

inline Mat4 lookAt(V3 eye, V3 at, V3 up) {
    V3 f = vNorm(vSub(at, eye));
    V3 s = vNorm(vCross(f, up));
    V3 u = vCross(s, f);
    Mat4 r{};
    r.m[0] = s.x;
    r.m[1] = u.x;
    r.m[2] = -f.x;
    r.m[3] = 0;
    r.m[4] = s.y;
    r.m[5] = u.y;
    r.m[6] = -f.y;
    r.m[7] = 0;
    r.m[8] = s.z;
    r.m[9] = u.z;
    r.m[10] = -f.z;
    r.m[11] = 0;
    r.m[12] = -vDot(s, eye);
    r.m[13] = -vDot(u, eye);
    r.m[14] = vDot(f, eye);
    r.m[15] = 1;
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
