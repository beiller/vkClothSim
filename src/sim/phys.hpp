#pragma once
#include <algorithm>

struct PhysParams {
    int nCaps;
    float dt, damping, gravity, friction, skin, tension, stiff, maxStep;
};
static_assert(sizeof(PhysParams) == 36);

inline constexpr float kSkin = 0.01f;
inline constexpr float kSoftMaxStep = 0.05f;
inline constexpr float kCollideCompliance = 0.02f;
inline constexpr float kFrictionCompliance = 0.0f;
inline constexpr float kMinConstraintAlpha = 10.0f;

inline float constraintAlpha(const PhysParams& p) {
    float a = p.stiff > 1e-3f ? 10.0f / p.stiff : 1e3f;
    return std::max(a, kMinConstraintAlpha);
}
