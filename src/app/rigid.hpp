#pragma once
#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "api.hpp"
#include <vector>

class RigidScene {
public:
    void init();
    void addRigidBody(const RigidBody& body);
    void step();
    void setVelocitySteps(int n);
    std::vector<CapsulePose> capsulePose() const;

private:
    struct Body {
        JPH::BodyID id;
        CapsuleParams params;
    };

    std::vector<Body> m_bodies;
    JPH::PhysicsSettings m_settings;
    JPH::PhysicsSystem* m_phys = nullptr;
    JPH::JobSystemThreadPool* m_job = nullptr;
    JPH::TempAllocatorImpl* m_temp = nullptr;
    JPH::ObjectLayerPairFilterTable* m_pair = nullptr;
    JPH::BroadPhaseLayerInterfaceTable* m_bpl = nullptr;
    JPH::ObjectVsBroadPhaseLayerFilterTable* m_ovb = nullptr;
};
