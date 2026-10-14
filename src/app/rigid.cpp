#include "app/rigid.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstring>
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
}

int RigidScene::addCapsuleShape(const CapsuleParams& params) {
    JPH::ShapeRefC shape(new JPH::CapsuleShape(params.halfLen, params.radius));
    m_shapes.push_back({shape, ShapeKind::Capsule, params});
    return (int)m_shapes.size() - 1;
}

int RigidScene::addBoxShape(const V3& half) {
    JPH::BoxShapeSettings bs(JPH::Vec3(half.x, half.y, half.z));
    bs.SetEmbedded(); // shape outlives the scene; not released with the last body
    JPH::ShapeRefC shape = bs.Create().Get();
    m_shapes.push_back({shape, ShapeKind::Box, CapsuleParams{}});
    return (int)m_shapes.size() - 1;
}

ShapeKind RigidScene::shapeKind(int shapeId) const {
    return m_shapes[shapeId].kind;
}

const CapsuleParams& RigidScene::shapeParams(int shapeId) const {
    return m_shapes[shapeId].params;
}

JPH::BodyID RigidScene::addRigidBody(int shapeId, const V3& pos, const V4& quat, const RigidBodyProps& props,
                                     bool isStatic) {
    auto& bi = m_phys->GetBodyInterface();
    JPH::BodyCreationSettings cs(m_shapes[shapeId].shape, JPH::RVec3(pos.x, pos.y, pos.z), jQuat(quat),
                                 isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic, isStatic ? 0 : 1);
    cs.mFriction = props.friction;
    cs.mRestitution = props.restitution;
    cs.mLinearDamping = props.damping;
    if (isStatic) {
        JPH::Body* b = bi.CreateBody(cs);
        bi.AddBody(b->GetID(), JPH::EActivation::DontActivate);
        m_phys->OptimizeBroadPhase();
        return b->GetID();
    }
    return bi.CreateAndAddBody(cs, JPH::EActivation::Activate);
}

JPH::BodyID RigidScene::addKinematicBody(const V3& pos, const V4& quat, float radius) {
    // kinematic = user-driven anchor for joints; infinite mass in the solver, no integration
    auto& bi = m_phys->GetBodyInterface();
    JPH::BodyCreationSettings cs(new JPH::SphereShape(radius), JPH::RVec3(pos.x, pos.y, pos.z), jQuat(quat),
                                 JPH::EMotionType::Kinematic, 1);
    return bi.CreateAndAddBody(cs, JPH::EActivation::Activate);
}

void RigidScene::setKinematicPose(JPH::BodyID id, const V3& pos, const V4& quat) {
    // kinematic body: Jolt derives its velocity from the position change (used by the solver/broadphase)
    auto& bi = m_phys->GetBodyInterface();
    bi.SetPositionAndRotation(id, JPH::RVec3(pos.x, pos.y, pos.z), jQuat(quat), JPH::EActivation::Activate);
}

int RigidScene::createJoint(JPH::BodyID a, JPH::BodyID b, const char* type, const V3& grabPos) {
    if (std::strcmp(type, "point") != 0)
        return -1;
    // no body locking: single-threaded between Update() calls, so the references stay valid
    JPH::Body* bodyA = m_phys->GetBodyLockInterface().TryGetBody(a);
    JPH::Body* bodyB = m_phys->GetBodyLockInterface().TryGetBody(b);
    if (!bodyA || !bodyB)
        return -1;
    JPH::PointConstraintSettings cs;
    // LocalToBodyCOM: express the grab (collision) point in each body's COM-local space so the
    // grabbed point on the target follows the hand's grip, not the two centers of mass.
    const JPH::RVec3 grab(grabPos.x, grabPos.y, grabPos.z);
    cs.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
    cs.mPoint1 = bodyA->GetRotation().InverseRotate(grab - bodyA->GetPosition());
    cs.mPoint2 = bodyB->GetRotation().InverseRotate(grab - bodyB->GetPosition());
    JPH::TwoBodyConstraint* constraint = cs.Create(*bodyA, *bodyB);
    m_phys->AddConstraint(constraint);
    m_joints.push_back({constraint, true});
    return (int)m_joints.size() - 1;
}

void RigidScene::destroyJoint(int jointId) {
    if (jointId < 0 || jointId >= (int)m_joints.size() || !m_joints[jointId].alive)
        return;
    // RemoveConstraint drops the manager's ref; releasing ours frees the constraint (never delete by hand)
    m_phys->RemoveConstraint(m_joints[jointId].constraint);
    m_joints[jointId].constraint = nullptr;
    m_joints[jointId].alive = false;
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

RayHit RigidScene::raycast(const V3& origin, const V3& dir, float maxDist) const {
    RayHit out{};
    // direction encodes the endpoint (length included): anything beyond maxDist is not reported
    JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), JPH::Vec3(dir.x, dir.y, dir.z) * maxDist);
    JPH::RayCastResult hit;
    if (!m_phys->GetNarrowPhaseQuery().CastRay(ray, hit))
        return out;
    const JPH::Vec3 hp = ray.GetPointOnRay(hit.mFraction);
    out.hit = true;
    out.body = hit.mBodyID;
    out.pos = {hp.GetX(), hp.GetY(), hp.GetZ()};
    return out;
}

bool RigidScene::isDynamic(JPH::BodyID id) const {
    JPH::BodyLockRead lock(m_phys->GetBodyLockInterface(), id);
    if (!lock.Succeeded())
        return false;
    return lock.GetBody().IsDynamic();
}
