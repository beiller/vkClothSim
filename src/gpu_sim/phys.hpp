#pragma once

struct PhysParams {
    int nCaps;
    float dt, damping, gravity, friction, skin, tension, stiff, maxStep;
    float floorY;
    float invSteps; // 1/steps: pin-weight normalization (step-independent stiffness)
};
static_assert(sizeof(PhysParams) == 44);

inline constexpr float kSkin = 0.01f;
inline constexpr float kSoftMaxStep = 0.05f;
