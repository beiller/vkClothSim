#pragma once
#include "math.hpp"

struct CapsuleGPU {
    float centerRadius[4];
    float quat[4];
    float halfLen[4];
};
static_assert(sizeof(CapsuleGPU) == 48);

inline void fillCapsule(CapsuleGPU& c, const V3& pos, const Quat& quat, float halfLen, float radiusA, float radiusB) {
    c.centerRadius[0] = pos.x;
    c.centerRadius[1] = pos.y;
    c.centerRadius[2] = pos.z;
    c.centerRadius[3] = radiusA; // radius at A (bottom, -Y end)
    c.quat[0] = quat.x;
    c.quat[1] = quat.y;
    c.quat[2] = quat.z;
    c.quat[3] = quat.w;
    c.halfLen[0] = halfLen;
    c.halfLen[1] = radiusB; // radius at B (top, +Y end); == radiusA for a plain capsule/sphere
    c.halfLen[2] = 0.0f;
    c.halfLen[3] = 0.0f;
}
