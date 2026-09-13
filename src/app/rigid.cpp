#include "app/rigid.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/RegisterTypes.h>

#include "math.hpp"
#include <algorithm>
#include <random>
#include <thread>

void RigidScene::init(bool single) {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    m_temp = new JPH::TempAllocatorImpl((size_t)10 * 1024 * 1024);
    const unsigned hw = std::max(std::thread::hardware_concurrency(), 1u);
    m_job = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, (int)(hw > 1 ? hw - 1 : 1));

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
    JPH::BodyCreationSettings gset(gshape, JPH::RVec3(0.0f, -1.0f, 0.0f), JPH::Quat::sIdentity(),
                                   JPH::EMotionType::Static, 0);
    JPH::Body* gb = bi.CreateBody(gset);
    bi.AddBody(gb->GetID(), JPH::EActivation::DontActivate);

    if (single) {
        JPH::BodyCreationSettings cs(new JPH::CapsuleShape(kCapsuleHalfLen, kCapsuleRadius),
                                     JPH::RVec3(0.0f, 1.5f, 0.0f), JPH::Quat::sIdentity(), JPH::EMotionType::Static, 1);
        m_caps.push_back(bi.CreateAndAddBody(cs, JPH::EActivation::DontActivate));
    } else {
        const int kNCaps = 50;
        std::mt19937 rng(12345); // NOLINT(bugprone-random-generator-seed)
        std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
        for (int i = 0; i < kNCaps; ++i) {
            float px = (rnd(rng) * 2.0f - 1.0f) * 2.0f;
            float py = 4.5f + 3.5f * rnd(rng);
            float pz = (rnd(rng) * 2.0f - 1.0f) * 2.0f;
            JPH::Quat rot = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), rnd(rng) * 2.0f * kPi) *
                            JPH::Quat::sRotation(JPH::Vec3::sAxisX(), (rnd(rng) * 2.0f - 1.0f) * 0.9f);
            JPH::BodyCreationSettings cs(new JPH::CapsuleShape(kCapsuleHalfLen, kCapsuleRadius), JPH::RVec3(px, py, pz),
                                         rot, JPH::EMotionType::Dynamic, 1);
            cs.mFriction = 0.7f;
            cs.mRestitution = 0.05f;
            cs.mLinearDamping = 0.05f;
            m_caps.push_back(bi.CreateAndAddBody(cs, JPH::EActivation::Activate));
        }
    }
    m_phys->OptimizeBroadPhase();
}

void RigidScene::step(int n) {
    for (int i = 0; i < n; ++i)
        m_phys->Update(kFrameDt, 1, m_temp, m_job);
}

std::vector<CapsuleGPU> RigidScene::capsuleGPU() const {
    std::vector<CapsuleGPU> data(m_caps.size());
    if (!m_phys)
        return data;
    const JPH::BodyLockInterface& li = m_phys->GetBodyLockInterface();
    for (size_t i = 0; i < m_caps.size(); ++i) {
        JPH::BodyLockRead lock(li, m_caps[i]);
        const JPH::Body& b = lock.GetBody();
        JPH::Vec3 p = b.GetPosition();
        JPH::Quat q = b.GetRotation();
        data[i].centerRadius[0] = p.GetX();
        data[i].centerRadius[1] = p.GetY();
        data[i].centerRadius[2] = p.GetZ();
        data[i].centerRadius[3] = kCapsuleRadius;
        data[i].quat[0] = q.GetX();
        data[i].quat[1] = q.GetY();
        data[i].quat[2] = q.GetZ();
        data[i].quat[3] = q.GetW();
        data[i].halfLen[0] = kCapsuleHalfLen;
    }
    return data;
}
