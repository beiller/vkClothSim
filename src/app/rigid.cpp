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
    return {q.x, q.y, q.z, q.w};
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

JPH::BodyID RigidScene::addRigidBody(const RigidBody& body) {
    auto& bi = m_phys->GetBodyInterface();
    JPH::BodyCreationSettings cs(new JPH::CapsuleShape(body.shape.halfLen, body.shape.radius),
                                 JPH::RVec3(body.position.x, body.position.y, body.position.z), jQuat(body.orientation),
                                 JPH::EMotionType::Dynamic, 1);
    cs.mFriction = body.props.friction;
    cs.mRestitution = body.props.restitution;
    cs.mLinearDamping = body.props.damping;
    return bi.CreateAndAddBody(cs, JPH::EActivation::Activate);
}

JPH::BodyID RigidScene::addStaticBody(const RigidBody& body) {
    auto& bi = m_phys->GetBodyInterface();
    JPH::BodyCreationSettings cs(new JPH::CapsuleShape(body.shape.halfLen, body.shape.radius),
                                 JPH::RVec3(body.position.x, body.position.y, body.position.z), jQuat(body.orientation),
                                 JPH::EMotionType::Static, 1);
    return bi.CreateAndAddBody(cs, JPH::EActivation::DontActivate);
}

void RigidScene::setStaticPose(JPH::BodyID id, const Transform& t) {
    auto& bi = m_phys->GetBodyInterface();
    bi.SetPositionAndRotation(id, JPH::RVec3(t.pos.x, t.pos.y, t.pos.z), jQuat(t.quat), JPH::EActivation::DontActivate);
}

void RigidScene::step() {
    m_settings.mNumVelocitySteps = (unsigned)velocitySteps;
    m_phys->SetPhysicsSettings(m_settings);
    m_phys->Update(kFrameDt, 1, m_temp, m_job);
}

Transform RigidScene::pose(JPH::BodyID id) const {
    JPH::BodyLockRead lock(m_phys->GetBodyLockInterface(), id);
    const JPH::Body& b = lock.GetBody();
    const JPH::Vec3 p = b.GetPosition();
    const JPH::Quat q = b.GetRotation();
    return {{p.GetX(), p.GetY(), p.GetZ()}, {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}};
}
