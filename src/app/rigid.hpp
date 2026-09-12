// rigid.hpp
// The rigid-body physics (Jolt): a static ground plane + 50 dynamic capsules that fall and
// clump into a pile. Decoupled from the renderer (Vulkan) and the soft-body simulator
// (sim/): it exposes the capsules' transforms as plain data — sim::Collider segments for
// the one-way soft-body collision, and CapsuleGPU (per-instance) for the instanced render.
#pragma once
#include <vector>
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include "sim/sim.hpp"

// Per-instance GPU data for the instanced capsule render (mirrors the GLSL struct; 48 bytes).
struct CapsuleGPU { float centerRadius[4]; float quat[4]; float halfLen[4]; };
static_assert(sizeof(CapsuleGPU) == 48);

class RigidScene {
public:
    void init();                 // create the physics system, the ground, the 50 capsules
    void step(int n);            // run `n` physics sub-steps
    int capsuleCount() const { return (int)m_caps.size(); }
    // The capsules' current transforms as plain segments (one-way soft-body collision).
    std::vector<sim::Collider> colliders() const;
    // The capsules' current transforms as per-instance GPU data (instanced render).
    std::vector<CapsuleGPU> capsuleGPU() const;

private:
    struct Cap { JPH::BodyID id; float radius; float halfLen; };
    std::vector<Cap> m_caps;
    JPH::PhysicsSystem* m_phys = nullptr;
    JPH::JobSystemThreadPool* m_job = nullptr;
    JPH::TempAllocatorImpl* m_temp = nullptr;
    JPH::ObjectLayerPairFilterTable* m_pair = nullptr;
    JPH::BroadPhaseLayerInterfaceTable* m_bpl = nullptr;
    JPH::ObjectVsBroadPhaseLayerFilterTable* m_ovb = nullptr;
};
