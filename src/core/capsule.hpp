#pragma once
#include "math.hpp"

struct CapsuleGPU {
    float centerRadius[4];
    float quat[4];
    float halfLen[4];
};
static_assert(sizeof(CapsuleGPU) == 48);

inline void fillCapsule(CapsuleGPU& c, const V3& pos, const V4& quat, float halfLen, float radius) {
    c.centerRadius[0] = pos.x;
    c.centerRadius[1] = pos.y;
    c.centerRadius[2] = pos.z;
    c.centerRadius[3] = radius;
    c.quat[0] = quat.x;
    c.quat[1] = quat.y;
    c.quat[2] = quat.z;
    c.quat[3] = quat.w;
    c.halfLen[0] = halfLen;
}
