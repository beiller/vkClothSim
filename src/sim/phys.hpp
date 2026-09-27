#pragma once

struct PhysParams {
    int nCaps;
    float dt, damping, gravity, friction, skin, tension, stiff, maxStep;
};
static_assert(sizeof(PhysParams) == 36);

inline constexpr float kSkin = 0.01f;
inline constexpr float kSoftMaxStep = 0.05f;
