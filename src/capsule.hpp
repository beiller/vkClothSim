#pragma once
#include "math.hpp"
#include <cstdint>

inline constexpr int kCapPhiSegs = 20;
inline constexpr int kCapYRows = 32;
inline constexpr uint32_t kCapVPC = (uint32_t)(kCapYRows + 1) * kCapPhiSegs;

struct CapsuleParams {
    float halfLen;
    float radius;
};

struct CapsulePose {
    V3 pos;
    V4 quat;
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
