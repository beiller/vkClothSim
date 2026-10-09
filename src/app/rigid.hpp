#pragma once
#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "capsule.hpp"
#include "ecs.hpp"
#include "math.hpp"

struct RigidBody {
    CapsuleParams shape;
    V3 position{0, 0, 0};
    V4 orientation{0, 0, 0, 1};
    RigidBodyProps props;
};

struct RayHit {
    bool hit = false;
    JPH::BodyID body{};
    V3 pos{};    // world hit point
    V3 normal{}; // world hit normal
};

class RigidScene {
public:
    void init();
    JPH::BodyID addRigidBody(const RigidBody& body);
    JPH::BodyID addStaticBody(const RigidBody& body);
    void setStaticPose(JPH::BodyID id, const Transform& t);
    void step();
    Transform pose(JPH::BodyID id) const;
    RayHit raycast(const V3& origin, const V3& dir, float maxDist) const;
    // drive a dynamic body to a pose with zero velocity (held by a controller)
    void moveBody(JPH::BodyID id, const V3& pos, const V4& quat);
    void setVelocity(JPH::BodyID id, const V3& linVel);
    bool isDynamic(JPH::BodyID id) const;

    int velocitySteps = 10; // applied in step(); the UI slider binds to it

private:
    JPH::PhysicsSettings m_settings;
    JPH::PhysicsSystem* m_phys = nullptr;
    JPH::JobSystemThreadPool* m_job = nullptr;
    JPH::TempAllocatorImpl* m_temp = nullptr;
    JPH::ObjectLayerPairFilterTable* m_pair = nullptr;
    JPH::BroadPhaseLayerInterfaceTable* m_bpl = nullptr;
    JPH::ObjectVsBroadPhaseLayerFilterTable* m_ovb = nullptr;
};
