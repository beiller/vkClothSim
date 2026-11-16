#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kHeadlessHz = 60.0f;
inline constexpr float kHeadlessDt = 1.0f / kHeadlessHz;
inline constexpr float kMaxPhysDt = 1.0f / 30.0f;

// Fixed-timestep physics (see FRAME_PACING.md): integer-ns accumulator for wall-clock bookkeeping,
// constant float tick dt passed to the sim (never accumulated, so no float drift in it).
inline constexpr int kPhysHzDefault = 60;                       // default tick rate (--phys-hz)
inline constexpr int64_t kMaxFrameNs = 100'000'000;             // frame-time clamp: sim slows, never spirals
inline constexpr int64_t kMaxSimPerFrameNs = 67'000'000;        // max sim time per frame (~4 ticks at 60 Hz)
inline constexpr int64_t kHeadlessFrameNs = 1'000'000'000 / 60; // virtual headless frame: exactly 1 tick/frame

using V2 = glm::vec2;
using V3 = glm::vec3;
using V4 = glm::vec4;
using Quat = glm::quat;
using Mat4 = glm::mat4;

inline float srgbToLinear(float s) {
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

inline void linearizeRgba(std::vector<uint8_t>& rgba) {
    for (size_t i = 0; i < rgba.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            rgba[i + c] = (uint8_t)std::lround(srgbToLinear(rgba[i + c] / 255.0f) * 255.0f); // NOLINT(bugprone-narrowing-conversions): result is clamped to [0,255]
}


struct View {
    V3 pos{0, 0, 0};
    Quat quat{1.0f, 0.0f, 0.0f, 0.0f};
    float tanL = 1.0f, tanR = 1.0f, tanU = 0.58f, tanD = 0.58f;
};

inline Mat4 poseView(const V3& pos, const Quat& quat) {
    const Mat4 rot = mat4_cast(glm::inverse(quat));
    return rot * glm::translate(Mat4(1.0f), -pos);
}

inline Mat4 projFov(float tl, float tr, float tu, float td, float nearP, float farP) {
    return glm::frustum(tl * nearP, tr * nearP, td * nearP, tu * nearP, nearP, farP);
}
