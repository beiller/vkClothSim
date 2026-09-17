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
