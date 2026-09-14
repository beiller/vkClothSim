#include "app/rigid.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <thread>

namespace {

JPH::Quat jQuat(V4 q) {
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

} // namespace

void RigidScene::init() {
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
    m_phys->OptimizeBroadPhase();
}

int RigidScene::addCapsule(const CapsuleCollider& collider) {
    auto& bi = m_phys->GetBodyInterface();
    const CapsuleParams& params = collider.params();
    const V3 pos = collider.position();
    const V4 quat = collider.orientation();
    JPH::BodyCreationSettings cs(new JPH::CapsuleShape(params.halfLen, params.radius),
                                 JPH::RVec3(pos.x, pos.y, pos.z), jQuat(quat), JPH::EMotionType::Dynamic, 1);
    cs.mFriction = 0.7f;
    cs.mRestitution = 0.05f;
    cs.mLinearDamping = 0.05f;
    JPH::BodyID id = bi.CreateAndAddBody(cs, JPH::EActivation::Activate);
    m_bodies.push_back({id, params});
    return (int)m_bodies.size() - 1;
}

void RigidScene::updateCollider(int handle, V3 pos, V4 quat) {
    auto& bi = m_phys->GetBodyInterface();
    bi.SetPosition(m_bodies[handle].id, JPH::RVec3(pos.x, pos.y, pos.z), JPH::EActivation::Activate);
    bi.SetRotation(m_bodies[handle].id, jQuat(quat), JPH::EActivation::Activate);
}

void RigidScene::step(int n) {
    for (int i = 0; i < n; ++i)
        m_phys->Update(kFrameDt, 1, m_temp, m_job);
}

std::vector<CapsulePose> RigidScene::capsulePose() const {
    std::vector<CapsulePose> out(m_bodies.size());
    if (!m_phys)
        return out;
    const JPH::BodyLockInterface& li = m_phys->GetBodyLockInterface();
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        JPH::BodyLockRead lock(li, m_bodies[i].id);
        const JPH::Body& b = lock.GetBody();
        JPH::Vec3 p = b.GetPosition();
        JPH::Quat q = b.GetRotation();
        out[i].pos = {p.GetX(), p.GetY(), p.GetZ()};
        out[i].quat = {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
    }
    return out;
}

std::vector<CapsuleGPU> RigidScene::capsuleGPU() const {
    std::vector<CapsuleGPU> out(m_bodies.size());
    if (!m_phys)
        return out;
    const JPH::BodyLockInterface& li = m_phys->GetBodyLockInterface();
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        JPH::BodyLockRead lock(li, m_bodies[i].id);
        const JPH::Body& b = lock.GetBody();
        JPH::Vec3 p = b.GetPosition();
        JPH::Quat q = b.GetRotation();
        const CapsuleParams& cp = m_bodies[i].params;
        out[i].centerRadius[0] = p.GetX();
        out[i].centerRadius[1] = p.GetY();
        out[i].centerRadius[2] = p.GetZ();
        out[i].centerRadius[3] = cp.radius;
        out[i].quat[0] = q.GetX();
        out[i].quat[1] = q.GetY();
        out[i].quat[2] = q.GetZ();
        out[i].quat[3] = q.GetW();
        out[i].halfLen[0] = cp.halfLen;
    }
    return out;
}
