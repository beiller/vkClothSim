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

class CapsuleCollider {
public:
    explicit CapsuleCollider(CapsuleParams params) : m_params(params) {}
    CapsuleCollider(CapsuleParams params, V3 position, V4 orientation)
        : m_params(params), m_position(position), m_orientation(orientation) {}
    const CapsuleParams& params() const { return m_params; }
    V3 position() const { return m_position; }
    V4 orientation() const { return m_orientation; }

private:
    CapsuleParams m_params;
    V3 m_position{0.0f, 0.0f, 0.0f};
    V4 m_orientation{0.0f, 0.0f, 0.0f, 1.0f};
};

inline CapsuleCollider capsuleCollider(CapsuleParams params) {
    return CapsuleCollider(params);
}
inline CapsuleCollider capsuleCollider(CapsuleParams params, V3 position, V4 orientation) {
    return CapsuleCollider(params, position, orientation);
}
