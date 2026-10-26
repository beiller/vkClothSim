#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
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

// Note: the 4-arg Quat constructor is (w, x, y, z) — w first; memory stays [x,y,z,w].

// a view frustum: position + orientation + per-edge tangent extents
struct View {
    V3 pos{0, 0, 0};
    Quat quat{1.0f, 0.0f, 0.0f, 0.0f};
    float tanL = 1.0f, tanR = 1.0f, tanU = 0.58f, tanD = 0.58f;
};

// the world-to-camera (view) matrix of a pose: [Q^T | -Q^T * pos]
inline Mat4 poseView(const V3& pos, const Quat& quat) {
    const Mat4 rot = mat4_cast(glm::inverse(quat));
    return rot * glm::translate(Mat4(1.0f), -pos);
}

// asymmetric projection from per-edge tangent extents (VR eyes)
inline Mat4 projFov(float tl, float tr, float tu, float td, float nearP, float farP) {
    return glm::frustum(tl * nearP, tr * nearP, td * nearP, tu * nearP, nearP, farP);
}
