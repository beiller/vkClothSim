#pragma once
#include "math.hpp"
#include <cstdint>

struct CapsuleParams {
    float halfLen;
    float radius;
};

struct CapsuleGPU {
    float centerRadius[4];
    float quat[4];
    float halfLen[4];
};
static_assert(sizeof(CapsuleGPU) == 48);

inline constexpr float kCapsuleRadius = 0.5f;
inline constexpr float kCapsuleHalfLen = 0.9f;
inline constexpr CapsuleParams kCapsule{kCapsuleHalfLen, kCapsuleRadius};
