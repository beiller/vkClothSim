#include "app/rigid.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCapsuleShape.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

JPH::Quat jQuat(V4 q) {
    return {q.x, q.y, q.z, q.w};
}

JPH::ShapeRefC applyBodyScale(JPH::ShapeRefC shape, const V3& s) {
    if (s.x == 1.0f && s.y == 1.0f && s.z == 1.0f)
        return shape;
    JPH::Shape::ShapeResult r = shape->ScaleShape(JPH::Vec3(s.x, s.y, s.z));
    if (!r.Get())
        std::fprintf(stderr, "3dsim: rigid body scale {%g, %g, %g} rejected by Jolt (zero scale?)\n", s.x, s.y, s.z);
    return r.Get() ? r.Get() : shape;
}

}

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

void RigidScene::reset() {
    if (!m_phys)
        return;
    for (const auto& [e, id] : m_body)
        m_phys->GetBodyInterface().RemoveBody(id);
    m_body.clear();
    m_shape.clear();
    for (int i = (int)m_joints.size() - 1; i >= 0; --i)
        releaseJoint(i);
    m_joints.clear();
    m_jointIdx.clear();
}

JPH::ShapeRefC RigidScene::buildShape(const std::vector<ColliderDesc>& colliders, const V3& scale) {
    const auto isIdentity = [](const Transform& t) {
        return t.pos.x == 0.0f && t.pos.y == 0.0f && t.pos.z == 0.0f && t.quat.x == 0.0f && t.quat.y == 0.0f &&
               t.quat.z == 0.0f && t.quat.w == 1.0f;
    };
    if (colliders.size() == 1 && isIdentity(colliders[0].local))
        return applyBodyScale(colliders[0].shape, scale);
    JPH::StaticCompoundShapeSettings compound;
    for (const ColliderDesc& c : colliders)
        compound.AddShape(JPH::Vec3(c.local.pos.x, c.local.pos.y, c.local.pos.z), jQuat(c.local.quat), c.shape);
    return applyBodyScale(compound.Create().Get(), scale);
}

std::vector<ColliderDesc> RigidScene::colliderDescs(const entt::registry& reg,
                                                    const std::vector<entt::entity>& cols) {
    std::vector<ColliderDesc> out;
    for (entt::entity c : cols) {
        const auto* col = reg.try_get<Collider>(c);
        if (!col)
            continue;
        const JPH::Shape* s = shape(reg, col->shape);
        if (!s)
            continue;
        const Transform t = reg.try_get<Transform>(c) ? reg.get<Transform>(c) : Transform{};
        out.push_back({s, t});
    }
    return out;
}

void RigidScene::buildBody(entt::entity e, const std::vector<ColliderDesc>& colliders, const RigidBody& body,
                           const Transform& t) {
    if (colliders.empty())
        return;
    auto& bi = m_phys->GetBodyInterface();
    JPH::ShapeRefC shape = buildShape(colliders, t.scale);
    const bool isStatic = body.isStatic;
    JPH::BodyCreationSettings cs(shape, JPH::RVec3(t.pos.x, t.pos.y, t.pos.z), jQuat(t.quat),
                                 isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic, isStatic ? 0 : 1);
    cs.mFriction = body.friction;
    cs.mRestitution = body.restitution;
    cs.mLinearDamping = body.damping;
    JPH::BodyID id;
    if (isStatic) {
        JPH::Body* b = bi.CreateBody(cs);
        bi.AddBody(b->GetID(), JPH::EActivation::DontActivate);
        m_phys->OptimizeBroadPhase();
        id = b->GetID();
    } else
        id = bi.CreateAndAddBody(cs, JPH::EActivation::Activate);
    m_body[e] = id;
}

void RigidScene::buildKinematic(entt::entity e, const std::vector<ColliderDesc>& colliders, const Transform& t) {
    if (colliders.empty())
        return;
    auto& bi = m_phys->GetBodyInterface();
    JPH::BodyCreationSettings cs(buildShape(colliders, t.scale), JPH::RVec3(t.pos.x, t.pos.y, t.pos.z), jQuat(t.quat),
                                 JPH::EMotionType::Kinematic, 1);
    m_body[e] = bi.CreateAndAddBody(cs, JPH::EActivation::Activate);
}

void RigidScene::removeBody(entt::entity e) {
    const auto it = m_body.find(e);
    if (it == m_body.end())
        return;
    const JPH::BodyID id = it->second;
    m_body.erase(it);
    m_phys->GetBodyInterface().RemoveBody(id);
    for (size_t i = 0; i < m_joints.size(); ++i) {
        if (!m_joints[i].alive)
            continue;
        const auto* two = static_cast<const JPH::TwoBodyConstraint*>(m_joints[i].constraint.GetPtr());
        if (two->GetBody1()->GetID() == id || two->GetBody2()->GetID() == id) {
            m_jointIdx.erase(m_joints[i].ent);
            releaseJoint((int)i);
        }
    }
}

bool RigidScene::hasBody(entt::entity e) const {
    return m_body.count(e) > 0;
}

entt::entity RigidScene::entityFor(JPH::BodyID id) const {
    for (const auto& [e, b] : m_body)
        if (b == id)
            return e;
    return entt::null;
}

const JPH::Shape* RigidScene::shape(const entt::registry& reg, entt::entity e) {
    const auto it = m_shape.find(e);
    if (it != m_shape.end())
        return it->second.GetPtr();
    JPH::ShapeRefC s;
    if (const auto* cs = reg.try_get<CapsuleShape>(e))
        s = JPH::ShapeRefC(new JPH::CapsuleShape(cs->halfLen, cs->radius));
    else if (const auto* bs = reg.try_get<BoxShape>(e))
        s = JPH::ShapeRefC(new JPH::BoxShape(JPH::Vec3(bs->half.x, bs->half.y, bs->half.z)));
    else if (const auto* tc = reg.try_get<TaperedCapsule>(e)) {
        // Create() may return a (rotated-translated) sphere for degenerate tapers
        const JPH::TaperedCapsuleShapeSettings settings(tc->halfHeight, tc->topRadius, tc->bottomRadius);
        const JPH::Shape::ShapeResult r = settings.Create();
        if (!r.Get())
            return nullptr;
        s = r.Get();
    }
    if (!s.GetPtr())
        return nullptr;
    const JPH::Shape* ptr = s.GetPtr();
    m_shape.emplace(e, std::move(s));
    return ptr;
}

void RigidScene::onBodyDestroyed(entt::registry&, entt::entity e) {
    removeBody(e);
}

void RigidScene::onShapeDestroyed(entt::registry&, entt::entity e) {
    m_shape.erase(e);
}

void RigidScene::watch(entt::registry& reg) {
    reg.on_destroy<RigidBody>().connect<&RigidScene::onBodyDestroyed>(this);
    reg.on_destroy<KinematicBody>().connect<&RigidScene::onBodyDestroyed>(this);
    reg.on_destroy<Joint>().connect<&RigidScene::onJointDestroyed>(this);
    reg.on_destroy<CapsuleShape>().connect<&RigidScene::onShapeDestroyed>(this);
    reg.on_destroy<BoxShape>().connect<&RigidScene::onShapeDestroyed>(this);
    reg.on_destroy<TaperedCapsule>().connect<&RigidScene::onShapeDestroyed>(this);
}

void RigidScene::setKinematicPose(entt::entity e, const V3& pos, const V4& quat) {
    const auto it = m_body.find(e);
    if (it == m_body.end())
        return;
    auto& bi = m_phys->GetBodyInterface();
    bi.SetPositionAndRotation(it->second, JPH::RVec3(pos.x, pos.y, pos.z), jQuat(quat), JPH::EActivation::Activate);
}

bool RigidScene::makeJoint(entt::entity e, const Joint& j) {
    if (j.type != 0) {
        std::fprintf(stderr, "3dsim: joint type %d not supported (0 = point)\n", j.type);
        return false;
    }
    const auto ia = m_body.find(j.bodyA);
    if (ia == m_body.end())
        return false;
    if (j.bodyB != entt::null && m_body.find(j.bodyB) == m_body.end())
        return false;
    JPH::Body* bodyA = m_phys->GetBodyLockInterface().TryGetBody(ia->second);
    JPH::Body* bodyB = j.bodyB == entt::null ? &JPH::Body::sFixedToWorld
                                             : m_phys->GetBodyLockInterface().TryGetBody(m_body[j.bodyB]);
    if (!bodyA || !bodyB)
        return false;
    JPH::PointConstraintSettings cs;
    const JPH::RVec3 grab(j.grabPos.x, j.grabPos.y, j.grabPos.z);
    cs.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
    cs.mPoint1 = bodyA->GetRotation().InverseRotate(grab - bodyA->GetPosition());
    cs.mPoint2 = bodyB->GetRotation().InverseRotate(grab - bodyB->GetPosition());
    JPH::TwoBodyConstraint* constraint = cs.Create(*bodyA, *bodyB);
    m_phys->AddConstraint(constraint);
    m_joints.push_back({e, constraint, true});
    return true;
}

void RigidScene::releaseJoint(int idx) {
    if (idx < 0 || idx >= (int)m_joints.size() || !m_joints[idx].alive)
        return;
    m_phys->RemoveConstraint(m_joints[idx].constraint);
    m_joints[idx].constraint = nullptr;
    m_joints[idx].alive = false;
}

void RigidScene::project(const entt::registry& reg) {
    for (auto [e, rb] : reg.view<RigidBody>().each()) {
        if (m_body.count(e))
            continue;
        const auto* t = reg.try_get<Transform>(e);
        if (!t)
            continue;
        buildBody(e, colliderDescs(reg, rb.colliders), rb, *t);
    }
    for (auto [e, kb] : reg.view<KinematicBody>().each()) {
        if (m_body.count(e))
            continue;
        const auto* t = reg.try_get<Transform>(e);
        if (!t)
            continue;
        buildKinematic(e, colliderDescs(reg, kb.colliders), *t);
    }
    for (auto [e, j] : reg.view<Joint>().each()) {
        if (m_jointIdx.count(e))
            continue;
        if (!makeJoint(e, j))
            continue;
        m_jointIdx[e] = (int)m_joints.size() - 1;
    }
}

void RigidScene::step() {
    m_settings.mNumVelocitySteps = (unsigned)velocitySteps;
    m_phys->SetPhysicsSettings(m_settings);
    m_phys->Update(kFrameDt, 1, m_temp, m_job);
}

Transform RigidScene::pose(entt::entity e) const {
    const auto it = m_body.find(e);
    if (it == m_body.end())
        return {};
    JPH::BodyLockRead lock(m_phys->GetBodyLockInterface(), it->second);
    const JPH::Body& b = lock.GetBody();
    const JPH::Vec3 p = b.GetPosition();
    const JPH::Quat q = b.GetRotation();
    return {{p.GetX(), p.GetY(), p.GetZ()}, {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}};
}

RayHit RigidScene::raycast(const V3& origin, const V3& dir, float maxDist,
                           const std::vector<entt::entity>& excludes) const {
    RayHit out{};
    JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), JPH::Vec3(dir.x, dir.y, dir.z) * maxDist);
    JPH::RayCastResult hit;
    JPH::IgnoreMultipleBodiesFilter filter; // a ray from inside a convex body hits it at 0; skip ours
    for (const entt::entity x : excludes)
        if (const auto it = m_body.find(x); it != m_body.end())
            filter.IgnoreBody(it->second);
    if (!m_phys->GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter))
        return out;
    const JPH::Vec3 hp = ray.GetPointOnRay(hit.mFraction);
    out.hit = true;
    out.body = hit.mBodyID;
    out.pos = {hp.GetX(), hp.GetY(), hp.GetZ()};
    return out;
}

void RigidScene::onJointDestroyed(entt::registry&, entt::entity e) {
    const auto it = m_jointIdx.find(e);
    if (it == m_jointIdx.end())
        return;
    releaseJoint(it->second);
    m_jointIdx.erase(it);
}
