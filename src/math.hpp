#pragma once
#include <cmath>
#include <numbers>

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kSqrt2 = std::numbers::sqrt2_v<float>;
inline constexpr float kFrameDt = 1.0f / 60.0f;

struct V2 {
    float x = 0.0f, y = 0.0f;
};

struct V3 {
    float x, y, z;
};

struct PointLight {
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
inline V3 vCross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
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
// inverse of a unit quaternion
inline V4 quatInv(V4 q) {
    return {-q.x, -q.y, -q.z, q.w};
}
// shortest-arc unit quaternion rotating `from` onto `to` (both should be normalized)
inline V4 quatFromTo(const V3& from, const V3& to) {
    const V3 c = vCross(from, to);
    const float d = vDot(from, to);
    if (d < -0.9999f)
        return V4{1.0f, 0.0f, 0.0f, 0.0f};
    V4 q{c.x, c.y, c.z, d + 1.0f};
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / l, q.y / l, q.z / l, q.w / l};
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

struct XrEyeData {
    V3 pos{0, 0, 0};
    V4 quat{0, 0, 0, 1};
    float tanL = 1.0f, tanR = 1.0f, tanU = 0.58f, tanD = 0.58f;
};

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

inline V3 quatRotate(const V4& q, V3 v) {
    const float qq[4] = {q.x, q.y, q.z, q.w};
    float m[9];
    quatToMat3(qq, m);
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z};
}

inline Mat4 viewFromPose(const V3& pos, const V4& q) {
    const float qq[4] = {q.x, q.y, q.z, q.w};
    float rot[9];
    quatToMat3(qq, rot);
    const V3 right{rot[0], rot[1], rot[2]};
    const V3 up{rot[3], rot[4], rot[5]};
    const V3 back{rot[6], rot[7], rot[8]};
    Mat4 r{};
    r.m[0] = right.x;
    r.m[1] = up.x;
    r.m[2] = back.x;
    r.m[4] = right.y;
    r.m[5] = up.y;
    r.m[6] = back.y;
    r.m[8] = right.z;
    r.m[9] = up.z;
    r.m[10] = back.z;
    r.m[12] = -vDot(right, pos);
    r.m[13] = -vDot(up, pos);
    r.m[14] = -vDot(back, pos);
    r.m[15] = 1.0f;
    return r;
}

// signed tangents (OpenXR convention: angleLeft/angleDown negative)
// matches Godot OpenXRUtil::XrMatrix4x4f_CreateProjection with GRAPHICS_OPENGL
inline Mat4 projFov(float tl, float tr, float tu, float td, float nearP, float farP) {
    const float w = tr - tl;
    const float h = tu - td;
    Mat4 r{};
    r.m[0] = 2.0f / w;
    r.m[5] = 2.0f / h;
    r.m[8] = (tr + tl) / w;
    r.m[9] = (tu + td) / h;
    r.m[10] = (farP + nearP) / (nearP - farP);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * farP * nearP / (nearP - farP);
    return r;
}
