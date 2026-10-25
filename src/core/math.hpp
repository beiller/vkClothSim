#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <numbers>

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kRigidHz = 120.0f; // Jolt (rigid) physics rate
inline constexpr float kSoftHz = 60.0f;  // cloth (soft) sim rate
inline constexpr float kFrameDt = 1.0f / kRigidHz;
inline constexpr int kSoftSubdiv = (int)(kRigidHz / kSoftHz); // rigid steps per cloth step
inline constexpr float kSoftDt = 1.0f / kSoftHz;

using V2 = glm::vec2;
using V3 = glm::vec3;
using V4 = glm::vec4;
using Quat = glm::quat;
using Mat4 = glm::mat4;

// a view frustum: position + orientation + per-edge tangent extents
struct View {
    V3 pos{0, 0, 0};
    V4 quat{0, 0, 0, 1};
    float tanL = 1.0f, tanR = 1.0f, tanU = 0.58f, tanD = 0.58f;
};

// V4 is the quat storage layout (reflected POD components); convert at the compute boundary.
// GLM's 4-component constructor takes (w, x, y, z) — first arg is w (WXYZ by default).
inline Quat quatFrom(V4 v) {
    return Quat(v.w, v.x, v.y, v.z);
}
inline V4 v4From(Quat q) {
    return V4(q.x, q.y, q.z, q.w);
}

// the world-to-camera (view) matrix of a pose: [Q^T | -Q^T * pos]
inline Mat4 poseView(const V3& pos, const V4& quat) {
    const Mat4 rot = mat4_cast(glm::inverse(quatFrom(quat)));
    return rot * glm::translate(Mat4(1.0f), -pos);
}

// the rotation aligning `from` to `to` (both assumed unit-length)
inline V4 quatFromTo(const V3& from, const V3& to) {
    const V3 c = glm::cross(from, to);
    const float d = glm::dot(from, to);
    if (d < -0.9999f)
        return V4{1.0f, 0.0f, 0.0f, 0.0f};
    V4 q{c.x, c.y, c.z, d + 1.0f};
    const float l = glm::length(q);
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}

// asymmetric projection from per-edge tangent extents (VR eyes)
inline Mat4 projFov(float tl, float tr, float tu, float td, float nearP, float farP) {
    const float w = tr - tl;
    const float h = tu - td;
    Mat4 r{};
    r[0].x = 2.0f / w;
    r[1].y = 2.0f / h;
    r[2].x = (tr + tl) / w;
    r[2].y = (tu + td) / h;
    r[2].z = (farP + nearP) / (nearP - farP);
    r[2].w = -1.0f;
    r[3].z = 2.0f * farP * nearP / (nearP - farP);
    return r;
}
