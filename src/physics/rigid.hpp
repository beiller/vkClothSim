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
#include <unordered_set>

struct RayHit {
    bool hit = false;
    JPH::BodyID body;
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
    void setKinematicPose(entt::entity e, const V3& pos, const Quat& quat, float dt);
    void project(const entt::registry& reg);
    void step(entt::registry& reg, float dt);
    Transform pose(entt::entity e) const;
    // Fixed-timestep interpolation (see FRAME_PACING.md): each tick shifts curr→prev and stores the
    // new Jolt pose; per frame, interpolateInto writes lerp(prev,curr,alpha) into dynamic bodies'
    // local Transforms (kinematic bodies are script-driven and excluded).
    struct PosePair {
        V3 prevPos{}, currPos{};
        Quat prevQuat{1.0f, 0.0f, 0.0f, 0.0f}, currQuat{1.0f, 0.0f, 0.0f, 0.0f};
    };
    void snapshotPose(entt::entity e, const Transform& p);
    void interpolateInto(entt::registry& reg, float alpha) const;
    RayHit raycast(const V3& origin, const V3& dir, float maxDist,
                   const std::vector<entt::entity>& excludes = {}) const;
    entt::entity entityFor(JPH::BodyID id) const;
    const JPH::Shape* shape(const entt::registry& reg, entt::entity e);
    void watch(entt::registry& reg);
    void setExcludes(entt::entity a, entt::entity b, bool on);

private:
    struct Applied {
        RigidBody body;
        uint64_t shapeHash = 0;
        bool kinematic = false;
        int sleep = -1; // last-applied sleep command: -1 unknown (new body, active), 0 awake, 1 asleep
    };
    std::vector<ColliderDesc> colliderDescs(const entt::registry& reg,
                                            const std::vector<entt::entity>& cols);
    static JPH::ShapeRefC buildShape(const std::vector<ColliderDesc>& colliders, const V3& scale);
    void buildBody(entt::entity e, const std::vector<ColliderDesc>& colliders, const RigidBody& body,
                   const Transform& t);
    void buildKinematic(entt::entity e, const std::vector<ColliderDesc>& colliders, const Transform& t);
    bool makeJoint(entt::entity e, const Joint& j);
    bool makeSixDofJoint(entt::entity e, const SixDofJoint& j);
    void releaseJoint(int idx);
    static uint64_t shapeFingerprint(const entt::registry& reg, const std::vector<entt::entity>& cols);
    static Applied appliedFor(const RigidBody& rb, uint64_t shapeHash, bool kinematic);
    void syncScalars(entt::entity e, const RigidBody& rb);
    void rebuildBody(entt::entity e, const entt::registry& reg, const RigidBody& rb, const Transform& t);
    void rebuildKinematic(entt::entity e, const entt::registry& reg, const std::vector<entt::entity>& cols,
                          const Transform& t);
    std::vector<entt::entity> affectedJoints(JPH::BodyID id) const;
    void rejoinJoints(const std::vector<entt::entity>& list, const entt::registry& reg);
    void onBodyDestroyed(entt::registry&, entt::entity e);
    void onShapeDestroyed(entt::registry&, entt::entity e);
    void onJointDestroyed(entt::registry&, entt::entity e);
    struct JointEntry {
        entt::entity ent = entt::null;
        JPH::Ref<JPH::Constraint> constraint;
        bool alive = false;
        entt::entity a = entt::null;
        entt::entity b = entt::null;
        bool exclude = false;
        SixDofJoint applied;
    };
    struct ExcludeFilter final : public JPH::GroupFilter {
        RigidScene* s = nullptr;
        bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override {
            const auto ea = static_cast<entt::entity>(a.GetGroupID());
            const auto eb = static_cast<entt::entity>(b.GetGroupID());
            return !s->excludes(ea, eb) && !s->excludes(eb, ea);
        }
    };
    bool excludes(entt::entity a, entt::entity b) const;
    void syncExcludes(entt::entity e);
    void syncJointExclusion(JointEntry& en, bool on);
    void syncSixDof(JointEntry& en, const SixDofJoint& j);

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
    std::unordered_map<entt::entity, PosePair> m_poses; // last two tick poses, for render interpolation
    std::unordered_map<entt::entity, JPH::ShapeRefC> m_shape;
    std::unordered_map<entt::entity, Applied> m_applied;
    std::unordered_set<entt::entity> m_forceSleep;
    JPH::Ref<ExcludeFilter> m_excludeFilter;
    std::unordered_map<entt::entity, std::unordered_set<entt::entity>> m_excludes;
};
