// rigid.cpp
#include "app/rigid.hpp"
#include <cstdlib>
#include <thread>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

void RigidScene::init() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    m_temp = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    unsigned hw = std::thread::hardware_concurrency();
    if (hw < 1)
        hw = 1;
    m_job = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, hw - 1 > 0 ? hw - 1 : 1);

    m_pair = new JPH::ObjectLayerPairFilterTable(2);
    m_pair->EnableCollision(0, 0);
    m_pair->EnableCollision(0, 1);
    m_pair->EnableCollision(1, 0);
    m_pair->EnableCollision(1, 1);
    m_bpl = new JPH::BroadPhaseLayerInterfaceTable(2, 2);
    m_bpl->MapObjectToBroadPhaseLayer(0, JPH::BroadPhaseLayer(0));
    m_bpl->MapObjectToBroadPhaseLayer(1, JPH::BroadPhaseLayer(1));
    m_ovb = new JPH::ObjectVsBroadPhaseLayerFilterTable(*m_bpl, 2, *m_pair, 2);

    m_phys = new JPH::PhysicsSystem();
    m_phys->Init(1024, 0, 1024, 2048, *m_bpl, *m_ovb, *m_pair);

    auto& bi = m_phys->GetBodyInterface();
    JPH::BoxShapeSettings gs(JPH::Vec3(40.0f, 1.0f, 40.0f));
    gs.SetEmbedded();
    JPH::ShapeRefC gshape = gs.Create().Get();
    JPH::BodyCreationSettings gset(gshape, JPH::RVec3(0.0f, -1.0f, 0.0f), JPH::Quat::sIdentity(), JPH::EMotionType::Static, 0);
    JPH::Body* gb = bi.CreateBody(gset);
    bi.AddBody(gb->GetID(), JPH::EActivation::DontActivate);

    srand(12345);
    // Dynamic capsules dropped in a column above the origin: they fall, bounce, and clump
    // into a pile. Low restitution + high friction so they settle into a mound.
    // Spawned in a 4x4 m footprint (wider for 50 so the spawn packing stays ~loose, not a
    // dense overlapping column) at staggered heights 4.5..8.0 — the cloud spawns BELOW the
    // held cloth (y=10), so no capsule pokes through the pinned sheet.
    const int kNCaps = 50;
    const float R = 0.5f, HL = 0.9f;
    auto rnd = [&] { return (rand() % 1000) / 1000.0f; };
    for (int i = 0; i < kNCaps; ++i) {
        float px = (rnd() * 2.0f - 1.0f) * 2.0f;
        float py = 4.5f + 3.5f * rnd();           // staggered heights 4.5..8.0 (top ~9.4 < 10)
        float pz = (rnd() * 2.0f - 1.0f) * 2.0f;
        JPH::Quat rot = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), rnd() * 6.28318f)
                      * JPH::Quat::sRotation(JPH::Vec3::sAxisX(), (rnd() * 2.0f - 1.0f) * 0.9f);
        JPH::BodyCreationSettings cs(new JPH::CapsuleShape(HL, R), JPH::RVec3(px, py, pz), rot,
                                     JPH::EMotionType::Dynamic, 1);
        cs.mFriction = 0.7f;
        cs.mRestitution = 0.05f;
        cs.mLinearDamping = 0.05f;
        JPH::BodyID id = bi.CreateAndAddBody(cs, JPH::EActivation::Activate);
        m_caps.push_back({id, R, HL});
    }
    m_phys->OptimizeBroadPhase();
}

void RigidScene::step(int n) {
    const float h = 1.0f / 60.0f;
    for (int i = 0; i < n; ++i)
        m_phys->Update(h, 1, m_temp, m_job);
}

std::vector<sim::Collider> RigidScene::colliders() const {
    std::vector<sim::Collider> out;
    if (!m_phys)
        return out;
    const JPH::BodyLockInterface& li = m_phys->GetBodyLockInterface();
    for (auto& c : m_caps) {
        JPH::BodyLockRead lk(li, c.id);
        const JPH::Body& b = lk.GetBody();
        JPH::Vec3 p = b.GetPosition();
        JPH::Vec3 axis = b.GetRotation() * JPH::Vec3(0.0f, c.halfLen, 0.0f);
        sim::Collider col;
        col.a[0] = p.GetX() - axis.GetX(); col.a[1] = p.GetY() - axis.GetY(); col.a[2] = p.GetZ() - axis.GetZ();
        col.b[0] = p.GetX() + axis.GetX(); col.b[1] = p.GetY() + axis.GetY(); col.b[2] = p.GetZ() + axis.GetZ();
        col.r = c.radius;
        out.push_back(col);
    }
    return out;
}

std::vector<CapsuleGPU> RigidScene::capsuleGPU() const {
    std::vector<CapsuleGPU> data(m_caps.size());
    if (!m_phys)
        return data;
    const JPH::BodyLockInterface& li = m_phys->GetBodyLockInterface();
    for (size_t i = 0; i < m_caps.size(); ++i) {
        JPH::BodyLockRead lock(li, m_caps[i].id);
        const JPH::Body& b = lock.GetBody();
        JPH::Vec3 p = b.GetPosition();
        JPH::Quat q = b.GetRotation();
        data[i].centerRadius[0] = p.GetX();
        data[i].centerRadius[1] = p.GetY();
        data[i].centerRadius[2] = p.GetZ();
        data[i].centerRadius[3] = m_caps[i].radius;
        data[i].quat[0] = q.GetX();
        data[i].quat[1] = q.GetY();
        data[i].quat[2] = q.GetZ();
        data[i].quat[3] = q.GetW();
        data[i].halfLen[0] = m_caps[i].halfLen;
    }
    return data;
}
