#pragma once
#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

#include "math.hpp"
#include "sim/params.hpp"
#include <entt/entt.hpp>
#include <string>

struct Transform {
    V3 pos{0, 0, 0};
    V4 quat{0, 0, 0, 1};

    Mat4 toMat4() const {
        const float q[4] = {quat.x, quat.y, quat.z, quat.w};
        float rot[9];
        quatToMat3(q, rot);
        Mat4 m{};
        m.m[0] = rot[0];
        m.m[1] = rot[1];
        m.m[2] = rot[2];
        m.m[4] = rot[3];
        m.m[5] = rot[4];
        m.m[6] = rot[5];
        m.m[8] = rot[6];
        m.m[9] = rot[7];
        m.m[10] = rot[8];
        m.m[12] = pos.x;
        m.m[13] = pos.y;
        m.m[14] = pos.z;
        m.m[15] = 1.0f;
        return m;
    }
};

struct RigidBodyProps {
    float friction = 0.7f;
    float restitution = 0.05f;
    float damping = 0.05f;
};

struct CapsuleCollider {
    int slot = -1;
};

struct RigidDynamics {
    RigidBodyProps props;
    JPH::BodyID id;
};

struct SoftBodyData {
    int softId = -1;
    int pinned = 0;
    SimParams params;
    int steps = kDefaultSteps;
};

struct Renderable {
    int mesh = -1;
    int inst = -1;
};

struct Material {
    V3 baseColor{1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};

struct Name {
    std::string id;
};

struct PinHold {
    float holdTime;
    float time = 0.0f;
};
