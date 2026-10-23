#pragma once

struct SimParams {
    float mass = 1.0f;
    float damping = 0.96f;
    int passes = 8;
    float stiffness = 1.0f;
    float tension = 1.0f;
    float friction = 2.0f;
};

inline constexpr int kDefaultSteps = 3;
