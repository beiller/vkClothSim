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
#include <unordered_map>

struct RayHit {
    bool hit = false;
    JPH::BodyID body{};
    V3 pos{};
};

struct ColliderDesc {
    const JPH::Shape* shape = nullptr;
    Transform local{};
};

class RigidScene {
public:
    void init();
    void reset();
    void removeBody(entt::entity e);
    bool hasBody(entt::entity e) const;
    void setKinematicPose(entt::entity e, const V3& pos, const V4& quat);
    void project(const entt::registry& reg);
    void step();
    Transform pose(entt::entity e) const;
    RayHit raycast(const V3& origin, const V3& dir, float maxDist,
                   const std::vector<entt::entity>& excludes = {}) const;
    entt::entity entityFor(JPH::BodyID id) const;
    const JPH::Shape* shape(const entt::registry& reg, entt::entity e);
    void watch(entt::registry& reg);

    int velocitySteps = 10;
    int positionSteps = 10;

private:
    std::vector<ColliderDesc> colliderDescs(const entt::registry& reg,
                                            const std::vector<entt::entity>& cols);
    JPH::ShapeRefC buildShape(const std::vector<ColliderDesc>& colliders, const V3& scale);
    void buildBody(entt::entity e, const std::vector<ColliderDesc>& colliders, const RigidBody& body,
                   const Transform& t);
    void buildKinematic(entt::entity e, const std::vector<ColliderDesc>& colliders, const Transform& t);
    bool makeJoint(entt::entity e, const Joint& j);
    bool makeSixDofJoint(entt::entity e, const SixDofJoint& j);
    void releaseJoint(int idx);
    void onBodyDestroyed(entt::registry&, entt::entity e);
    void onShapeDestroyed(entt::registry&, entt::entity e);
    void onJointDestroyed(entt::registry&, entt::entity e);
    struct JointEntry {
        entt::entity ent = entt::null;
        JPH::Ref<JPH::Constraint> constraint;
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
    std::unordered_map<entt::entity, int> m_jointIdx;
    std::unordered_map<entt::entity, JPH::BodyID> m_body;
    std::unordered_map<entt::entity, JPH::ShapeRefC> m_shape;
    std::unordered_map<entt::entity, JPH::RVec3> m_kinTarget;
};
