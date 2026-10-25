#pragma once
#include <cmath>
#include <numbers>

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kRigidHz = 120.0f; // Jolt (rigid) physics rate
inline constexpr float kSoftHz = 60.0f;  // cloth (soft) sim rate
inline constexpr float kFrameDt = 1.0f / kRigidHz;
inline constexpr int kSoftSubdiv = (int)(kRigidHz / kSoftHz); // rigid steps per cloth step
inline constexpr float kSoftDt = 1.0f / kSoftHz;

struct V2 {
    float x = 0.0f, y = 0.0f;
};

struct V3 {
    float x, y, z;
};

struct V4 {
    float x, y, z, w;
};

// a view frustum: position + orientation + per-edge tangent extents
struct View {
    V3 pos{0, 0, 0};
    V4 quat{0, 0, 0, 1};
    float tanL = 1.0f, tanR = 1.0f, tanU = 0.58f, tanD = 0.58f;
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
inline V3 vNormalize(V3 a) {
    const float l = vLen(a);
    return l > 1e-8f ? vScale(a, 1.0f / l) : V3{0, 0, 0};
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
inline V4 qNorm(V4 q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 1e-8f ? V4{q.x / l, q.y / l, q.z / l, q.w / l} : V4{0, 0, 0, 1};
}
inline V4 quatInv(V4 q) {
    return {-q.x, -q.y, -q.z, q.w};
}
inline V4 quatFromTo(const V3& from, const V3& to) {
    const V3 c = vCross(from, to);
    const float d = vDot(from, to);
    if (d < -0.9999f)
        return V4{1.0f, 0.0f, 0.0f, 0.0f};
    V4 q{c.x, c.y, c.z, d + 1.0f};
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}
// basis vectors are the rotation columns; the classic formula wants rows, so transpose
inline V4 basisToQuat(const V3& x, const V3& y, const V3& z) {
    const float a = x.x, b = x.y, c = x.z;
    const float d = y.x, e = y.y, f = y.z;
    const float g = z.x, h = z.y, i = z.z;
    const float tr = a + e + i;
    V4 q;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (f - h) / s;
        q.y = (g - c) / s;
        q.z = (b - d) / s;
    } else if (a > e && a > i) {
        const float s = std::sqrt(1.0f + a - e - i) * 2.0f;
        q.w = (f - h) / s;
        q.x = 0.25f * s;
        q.y = (d + b) / s;
        q.z = (g + c) / s;
    } else if (e > i) {
        const float s = std::sqrt(1.0f + e - a - i) * 2.0f;
        q.w = (g - c) / s;
        q.x = (d + b) / s;
        q.y = 0.25f * s;
        q.z = (h + f) / s;
    } else {
        const float s = std::sqrt(1.0f + i - a - e) * 2.0f;
        q.w = (b - d) / s;
        q.x = (h + f) / s;
        q.y = (g + c) / s;
        q.z = 0.25f * s;
    }
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len < 1e-6f)
        return {0, 0, 0, 1};
    return {q.x / len, q.y / len, q.z / len, q.w / len};
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

// inverse of an affine (linear + translation) transform
inline Mat4 mat4InvertAffine(const Mat4& M) {
    const float* m = M.m;
    float r[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = m[j * 4 + i];
    const float a = r[0], b = r[1], c = r[2], d = r[3], e = r[4], f = r[5], g = r[6], h = r[7], k = r[8];
    float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    if (det == 0.0f)
        det = 1.0f;
    const float idet = 1.0f / det;
    float invr[9];
    invr[0] = (e * k - f * h) * idet;
    invr[1] = (c * h - b * k) * idet;
    invr[2] = (b * f - c * e) * idet;
    invr[3] = (f * g - d * h) * idet;
    invr[4] = (a * k - c * g) * idet;
    invr[5] = (c * d - a * f) * idet;
    invr[6] = (d * h - e * g) * idet;
    invr[7] = (g * b - a * h) * idet;
    invr[8] = (a * e - b * d) * idet;
    Mat4 inv{};
    const float tx = m[12], ty = m[13], tz = m[14];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            inv.m[j * 4 + i] = invr[i * 3 + j];
    inv.m[12] = -(invr[0] * tx + invr[1] * ty + invr[2] * tz);
    inv.m[13] = -(invr[3] * tx + invr[4] * ty + invr[5] * tz);
    inv.m[14] = -(invr[6] * tx + invr[7] * ty + invr[8] * tz);
    inv.m[15] = 1.0f;
    return inv;
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

inline V3 quatRotate(const V4& q, V3 v) {
    const float qq[4] = {q.x, q.y, q.z, q.w};
    float m[9];
    quatToMat3(qq, m);
    return {m[0] * v.x + m[3] * v.y + m[6] * v.z, m[1] * v.x + m[4] * v.y + m[7] * v.z,
            m[2] * v.x + m[5] * v.y + m[8] * v.z};
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
