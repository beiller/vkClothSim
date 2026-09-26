#pragma once
#include <functional>
#include "capsule.hpp"
#include "mesh.hpp"
#include "sim/params.hpp"

namespace sim {
struct Constraint;
}

struct Timer {
    float time = 0.0f;
    std::function<void()> onExpire;
};

struct Collider {
    CapsuleParams params;
    V3 position{0, 0, 0};
    V4 orientation{0, 0, 0, 1};
};

struct RigidBody {
    Collider collider;
    float mass = 1.0f;
    float friction = 0.7f;
    float restitution = 0.05f;
    float damping = 0.05f;
};

struct SoftBody {
    Mesh mesh;
    std::vector<sim::Constraint> cons;
    SimParams params;
    bool pinned = false;
};

struct Camera {
    float fovDeg = 50.0f;
    float nearP = 0.1f;
    float farP = 300.0f;
    V3 position{0, 0, 0};
    V4 rotation{0, 0, 0, 1};

    Mat4 view() const {
        const float q[4] = {rotation.x, rotation.y, rotation.z, rotation.w};
        float rot[9];
        quatToMat3(q, rot);
        const V3 right{rot[0], rot[1], rot[2]};
        const V3 up{rot[3], rot[4], rot[5]};
        const V3 back{rot[6], rot[7], rot[8]};
        Mat4 r{};
        r.m[0] = right.x;
        r.m[1] = up.x;
        r.m[2] = back.x;
        r.m[4] = right.y;
        r.m[5] = up.y;
        r.m[6] = back.y;
        r.m[8] = right.z;
        r.m[9] = up.z;
        r.m[10] = back.z;
        r.m[12] = -vDot(right, position);
        r.m[13] = -vDot(up, position);
        r.m[14] = -vDot(back, position);
        r.m[15] = 1.0f;
        return r;
    }

    Mat4 viewProj(float aspect) const { return mul4(perspective(fovDeg, aspect, nearP, farP), view()); }
};
