#pragma once
#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "ecs.hpp"
#include "math.hpp"

struct RayHit {
    bool hit = false;
    JPH::BodyID body{};
    V3 pos{}; // world hit point
};

// one collider of a body: a (shared) Jolt shape + pose in the body's frame
struct ColliderDesc {
    const JPH::Shape* shape = nullptr;
    Transform local{};
};

class RigidScene {
public:
    void init();
    // rigid body from its colliders: a single identity-local collider reuses a shared shape,
    // anything else builds a compound. isStatic = fixed collider (ground) vs integrated.
    // scale = the entity's Transform scale, applied to the shape (Jolt bodies are rigid)
    JPH::BodyID addRigidBody(const RigidBody& body, const std::vector<ColliderDesc>& colliders, const V3& pos,
                             const V4& quat, const V3& scale, bool isStatic);
    // kinematic body = user-driven anchor for joints; infinite mass, pose set via setKinematicPose
    JPH::BodyID addKinematicBody(const std::vector<ColliderDesc>& colliders, const V3& pos, const V4& quat,
                                 const V3& scale);
    void setKinematicPose(JPH::BodyID id, const V3& pos, const V4& quat);
    void step();
    Transform pose(JPH::BodyID id) const;
    RayHit raycast(const V3& origin, const V3& dir, float maxDist, const std::vector<JPH::BodyID>& excludes = {}) const;
    // joint between two bodies; type = "point" (ball-socket); grabPos = world point the
    // joint anchors (the ray collision point); returns joint id or -1
    int createJoint(JPH::BodyID a, JPH::BodyID b, const char* type, const V3& grabPos);
    void destroyJoint(int jointId);
    bool isDynamic(JPH::BodyID id) const;

    int velocitySteps = 10; // applied in step(); the UI slider binds to it

private:
    // a single identity-local collider -> the shared shape directly; anything else a compound;
    // a non-unit scale wraps the result in a Jolt ScaledShape
    JPH::ShapeRefC buildShape(const std::vector<ColliderDesc>& colliders, const V3& scale);
    struct JointEntry {
        JPH::Ref<JPH::Constraint> constraint; // owning ref; RemoveConstraint drops the manager's, we drop ours
        bool alive = false;
    };

    JPH::PhysicsSettings m_settings;
    JPH::PhysicsSystem* m_phys = nullptr;
    JPH::JobSystemThreadPool* m_job = nullptr;
    JPH::TempAllocatorImpl* m_temp = nullptr;
    JPH::ObjectLayerPairFilterTable* m_pair = nullptr;
    JPH::BroadPhaseLayerInterfaceTable* m_bpl = nullptr;
    JPH::ObjectVsBroadPhaseLayerFilterTable* m_ovb = nullptr;
    std::vector<JointEntry> m_joints;
};
