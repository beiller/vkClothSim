#include "physics/rigid.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

// max kinematic velocity: the per-frame displacement clamp is this * dt, so the cap holds at any frame rate
constexpr float kMaxKinematicVel = 15.0f;

// normalize at the Jolt boundary: Jolt asserts on non-unit quats (e.g. user-script values)
JPH::Quat jQuat(Quat q) {
    const float l = glm::length(q);
    if (l < 1e-8f)
        q = Quat{1.0f, 0.0f, 0.0f, 0.0f};
    else
        q /= l;
    return {q.x, q.y, q.z, q.w};
}

JPH::Vec3 jVec(V3 v) {
    return {v.x, v.y, v.z};
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
    m_excludeFilter = new ExcludeFilter();
    m_excludeFilter->s = this;
}

void RigidScene::reset() {
    if (!m_phys)
        return;
    for (const auto& [e, id] : m_body)
        m_phys->GetBodyInterface().RemoveBody(id);
    m_body.clear();
    m_shape.clear();
    m_applied.clear();
    m_forceSleep.clear();
    m_excludes.clear();
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
    JPH::BodyCreationSettings cs(shape, JPH::RVec3(t.pos.x, t.pos.y, t.pos.z), jQuat(t.quat),
                                JPH::EMotionType::Dynamic, 1);
    cs.mFriction = body.friction;
    cs.mRestitution = body.restitution;
    cs.mLinearDamping = body.damping;
    if (body.mass > 0) {
        cs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        cs.mMassPropertiesOverride.mMass = body.mass;
    }
    m_body[e] = bi.CreateAndAddBody(cs, JPH::EActivation::Activate);
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
    m_applied.erase(e);
    m_forceSleep.erase(e);
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
    if (const auto ex = m_excludes.find(e); ex != m_excludes.end())
        for (entt::entity other : ex->second) {
            m_excludes[other].erase(e);
            syncExcludes(other);
        }
    m_excludes.erase(e);
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
    else if (const auto* ss = reg.try_get<SphereShape>(e))
        s = JPH::ShapeRefC(new JPH::SphereShape(ss->radius));
    else if (const auto* ch = reg.try_get<ConvexHullShape>(e)) {
        const auto& pts = ch->points;
        JPH::ConvexHullShapeSettings settings;
        settings.mPoints.reserve(pts.size() / 3);
        for (size_t i = 0; i + 2 < pts.size(); i += 3)
            settings.mPoints.push_back(JPH::Vec3(pts[i], pts[i + 1], pts[i + 2]));
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
    reg.on_destroy<SixDofJoint>().connect<&RigidScene::onJointDestroyed>(this);
    reg.on_destroy<CapsuleShape>().connect<&RigidScene::onShapeDestroyed>(this);
    reg.on_destroy<BoxShape>().connect<&RigidScene::onShapeDestroyed>(this);
    reg.on_destroy<TaperedCapsule>().connect<&RigidScene::onShapeDestroyed>(this);
    reg.on_destroy<SphereShape>().connect<&RigidScene::onShapeDestroyed>(this);
    reg.on_destroy<ConvexHullShape>().connect<&RigidScene::onShapeDestroyed>(this);
}

void RigidScene::setKinematicPose(entt::entity e, const V3& pos, const Quat& quat, float dt) {
    const auto it = m_body.find(e);
    if (it == m_body.end())
        return;
    auto& bi = m_phys->GetBodyInterface();
    const JPH::BodyID id = it->second;
    const JPH::RVec3 old = bi.GetPosition(id);
    const JPH::RVec3 p(pos.x, pos.y, pos.z);
    // MoveKinematic sets velocity only (Jolt integrates and lands on target at step end);
    // setting position too would move the body twice. Clamp to max travel per step.
    JPH::RVec3 delta = p - old;
    const float stepLen = JPH::Vec3(delta).Length();
    const float maxStep = kMaxKinematicVel * dt;
    if (stepLen > maxStep)
        delta = delta / stepLen * maxStep;
    bi.MoveKinematic(id, old + delta, jQuat(quat), dt);
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
    // LocalToBodyCOM is relative to the center of mass, not the shape origin (GetPosition());
    // baking from the origin leaves compound shapes with an off-origin centroid born-violated,
    // so the solver pulls the body by the COM offset at creation
    const JPH::RVec3 comA = bodyA->GetPosition() + bodyA->GetRotation() * bodyA->GetShape()->GetCenterOfMass();
    const JPH::RVec3 comB = bodyB->GetPosition() + bodyB->GetRotation() * bodyB->GetShape()->GetCenterOfMass();
    cs.mPoint1 = bodyA->GetRotation().InverseRotate(grab - comA);
    cs.mPoint2 = bodyB->GetRotation().InverseRotate(grab - comB);
    JPH::TwoBodyConstraint* constraint = cs.Create(*bodyA, *bodyB);
    m_phys->AddConstraint(constraint);
    // joints are baked from the current pose (satisfied), so activate now:
    // waiting for motion lets the first frame drift free, then the weld snaps back
    if (constraint->GetEnabled())
        m_phys->GetBodyInterface().ActivateConstraint(constraint);
    m_joints.push_back({e, constraint, true, j.bodyA, j.bodyB, j.excludeCollision});
    if (j.excludeCollision)
        setExcludes(j.bodyA, j.bodyB, true);
    return true;
}

bool RigidScene::makeSixDofJoint(entt::entity e, const SixDofJoint& j) {
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
    const JPH::RVec3 grab(j.grabPos.x, j.grabPos.y, j.grabPos.z);
    const JPH::Quat rel(j.relQuat.x, j.relQuat.y, j.relQuat.z, j.relQuat.w);
    using Axis = JPH::SixDOFConstraintSettings::EAxis;
    JPH::SixDOFConstraintSettings cs;
    cs.mSpace = j.worldSpace ? JPH::EConstraintSpace::WorldSpace : JPH::EConstraintSpace::LocalToBodyCOM;
    if (j.worldSpace) {
        cs.mPosition1 = grab;
        cs.mPosition2 = grab;
    } else {
        // LocalToBodyCOM is relative to the center of mass, not the shape origin (GetPosition());
        // baking from the origin leaves compound shapes with an off-origin centroid born-violated,
        // so the solver pulls the body by the COM offset at creation
        const JPH::RVec3 comA = bodyA->GetPosition() + bodyA->GetRotation() * bodyA->GetShape()->GetCenterOfMass();
        const JPH::RVec3 comB = bodyB->GetPosition() + bodyB->GetRotation() * bodyB->GetShape()->GetCenterOfMass();
        cs.mPosition1 = bodyA->GetRotation().InverseRotate(grab - comA);
        cs.mPosition2 = bodyB->GetRotation().InverseRotate(grab - comB);
    }
    const JPH::Quat axis = jQuat(j.axisQuat);
    if (j.axisQuat.x != 0.0f || j.axisQuat.y != 0.0f || j.axisQuat.z != 0.0f) {
        // custom limit frame: both bodies share the axisQuat world orientation at creation,
        // so limits are measured in that frame (it follows body A as it moves)
        if (j.worldSpace) {
            cs.mAxisX1 = axis * JPH::Vec3::sAxisX();
            cs.mAxisY1 = axis * JPH::Vec3::sAxisY();
            cs.mAxisX2 = axis * JPH::Vec3::sAxisX();
            cs.mAxisY2 = axis * JPH::Vec3::sAxisY();
        } else {
            cs.mAxisX1 = bodyA->GetRotation().InverseRotate(axis * JPH::Vec3::sAxisX());
            cs.mAxisY1 = bodyA->GetRotation().InverseRotate(axis * JPH::Vec3::sAxisY());
            cs.mAxisX2 = bodyB->GetRotation().InverseRotate(axis * JPH::Vec3::sAxisX());
            cs.mAxisY2 = bodyB->GetRotation().InverseRotate(axis * JPH::Vec3::sAxisY());
        }
    } else {
        cs.mAxisX2 = rel * JPH::Vec3::sAxisX();
        cs.mAxisY2 = rel * JPH::Vec3::sAxisY();
    }
    cs.mSwingType = j.pyramid ? JPH::ESwingType::Pyramid : JPH::ESwingType::Cone;
    cs.mConstraintPriority = (unsigned)j.priority;
    cs.mNumVelocityStepsOverride = (unsigned)j.velSteps;
    cs.mNumPositionStepsOverride = (unsigned)j.posSteps;
    cs.mEnabled = j.enabled;
    if (j.limOn) {
        // Jolt measures translation along (pos2 - pos1), so negate to get body A's displacement
        cs.SetLimitedAxis(Axis::TranslationX, -j.limTransMax.x, -j.limTransMin.x);
        cs.SetLimitedAxis(Axis::TranslationY, -j.limTransMax.y, -j.limTransMin.y);
        cs.SetLimitedAxis(Axis::TranslationZ, -j.limTransMax.z, -j.limTransMin.z);
        cs.SetLimitedAxis(Axis::RotationX, j.limAngMin.x, j.limAngMax.x);
        cs.SetLimitedAxis(Axis::RotationY, j.limAngMin.y, j.limAngMax.y);
        cs.SetLimitedAxis(Axis::RotationZ, j.limAngMin.z, j.limAngMax.z);
    }
    const float sf[3] = {j.softTransFreq.x, j.softTransFreq.y, j.softTransFreq.z};
    const float sd[3] = {j.softTransDamp.x, j.softTransDamp.y, j.softTransDamp.z};
    for (int i = 0; i < 3; ++i)
        cs.mLimitsSpringSettings[i] = JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, sf[i], sd[i]);
    const float ft[3] = {j.frictionTrans.x, j.frictionTrans.y, j.frictionTrans.z};
    const float fa[3] = {j.frictionAng.x, j.frictionAng.y, j.frictionAng.z};
    for (int i = 0; i < 3; ++i) {
        cs.mMaxFriction[i] = ft[i];
        cs.mMaxFriction[i + 3] = fa[i];
    }
    const auto mv = [](const std::vector<float>& v, int i, float def) {
        return i < (int)v.size() ? v[i] : def;
    };
    const float mf[3] = {j.motorFreq.x, j.motorFreq.y, j.motorFreq.z};
    const float md[3] = {j.motorDamp.x, j.motorDamp.y, j.motorDamp.z};
    const float fmin[3] = {j.motorForceMin.x, j.motorForceMin.y, j.motorForceMin.z};
    const float fmax[3] = {j.motorForceMax.x, j.motorForceMax.y, j.motorForceMax.z};
    const float tmin[3] = {j.motorTorqueMin.x, j.motorTorqueMin.y, j.motorTorqueMin.z};
    const float tmax[3] = {j.motorTorqueMax.x, j.motorTorqueMax.y, j.motorTorqueMax.z};
    for (int i = 0; i < 6; ++i) {
        if (mv(j.motorState, i, 0.f) <= 0.f)
            continue;
        cs.mMotorSettings[i].mSpringSettings =
            JPH::SpringSettings((JPH::ESpringMode)(int)mv(j.motorMode, i, 0.f), mf[i % 3], md[i % 3]);
        if (i < 3 && (fmin[i] != 0.f || fmax[i] != 0.f))
            cs.mMotorSettings[i].SetForceLimits(fmin[i], fmax[i]);
        if (i >= 3 && (tmin[i - 3] != 0.f || tmax[i - 3] != 0.f))
            cs.mMotorSettings[i].SetTorqueLimits(tmin[i - 3], tmax[i - 3]);
    }
    JPH::TwoBodyConstraint* constraint = cs.Create(*bodyA, *bodyB);
    auto* six = static_cast<JPH::SixDOFConstraint*>(constraint);
    for (int i = 0; i < 6; ++i)
        six->SetMotorState(Axis(i), JPH::EMotorState((int)mv(j.motorState, i, 0.f)));
    // targets are in Jolt's (pos2 - pos1) space, negate to express body A's motion;
    // orientation target is the relative frame rotation, so conjugate
    six->SetTargetPositionCS(JPH::Vec3(-j.motorTargetPos.x, -j.motorTargetPos.y, -j.motorTargetPos.z));
    six->SetTargetVelocityCS(JPH::Vec3(-j.motorTargetVel.x, -j.motorTargetVel.y, -j.motorTargetVel.z));
    six->SetTargetAngularVelocityCS(JPH::Vec3(-j.motorTargetAngVel.x, -j.motorTargetAngVel.y, -j.motorTargetAngVel.z));
    six->SetTargetOrientationCS(JPH::Quat(j.motorTargetQuat.x, j.motorTargetQuat.y, j.motorTargetQuat.z,
                                        j.motorTargetQuat.w).Conjugated());
    m_phys->AddConstraint(constraint);
    if (constraint->GetEnabled())
        m_phys->GetBodyInterface().ActivateConstraint(constraint);
    m_joints.push_back({e, constraint, true, j.bodyA, j.bodyB, j.excludeCollision});
    m_joints.back().applied = j;
    if (j.excludeCollision)
        setExcludes(j.bodyA, j.bodyB, true);
    return true;
}

static bool sixDofStructuralDiffers(const SixDofJoint& a, const SixDofJoint& b) {
    return a.bodyA != b.bodyA || a.bodyB != b.bodyB || a.worldSpace != b.worldSpace ||
           a.grabPos != b.grabPos || a.relQuat != b.relQuat || a.axisQuat != b.axisQuat ||
           a.pyramid != b.pyramid;
}

static bool sixDofDiffers(const SixDofJoint& a, const SixDofJoint& b) {
    if (sixDofStructuralDiffers(a, b))
        return true;
    return a.limOn != b.limOn || a.limTransMin != b.limTransMin || a.limTransMax != b.limTransMax ||
           a.limAngMin != b.limAngMin || a.limAngMax != b.limAngMax ||
           a.softTransFreq != b.softTransFreq || a.softTransDamp != b.softTransDamp ||
           a.frictionTrans != b.frictionTrans || a.frictionAng != b.frictionAng ||
           a.motorState != b.motorState || a.motorMode != b.motorMode ||
           a.motorFreq != b.motorFreq || a.motorDamp != b.motorDamp ||
           a.motorForceMin != b.motorForceMin || a.motorForceMax != b.motorForceMax ||
           a.motorTorqueMin != b.motorTorqueMin || a.motorTorqueMax != b.motorTorqueMax ||
           a.motorTargetPos != b.motorTargetPos || a.motorTargetVel != b.motorTargetVel ||
           a.motorTargetAngVel != b.motorTargetAngVel || a.motorTargetQuat != b.motorTargetQuat ||
           a.priority != b.priority || a.velSteps != b.velSteps || a.posSteps != b.posSteps ||
           a.enabled != b.enabled;
}

void RigidScene::syncSixDof(JointEntry& en, const SixDofJoint& j) {
    if (!en.alive)
        return;
    auto* six = static_cast<JPH::SixDOFConstraint*>(en.constraint.GetPtr());
    using Axis = JPH::SixDOFConstraintSettings::EAxis;
    // Jolt measures translation along (pos2 - pos1), so negate to express body A's motion
    if (j.limOn) {
        six->SetTranslationLimits(JPH::Vec3(-j.limTransMax.x, -j.limTransMax.y, -j.limTransMax.z),
                                 JPH::Vec3(-j.limTransMin.x, -j.limTransMin.y, -j.limTransMin.z));
        six->SetRotationLimits(JPH::Vec3(j.limAngMin.x, j.limAngMin.y, j.limAngMin.z),
                             JPH::Vec3(j.limAngMax.x, j.limAngMax.y, j.limAngMax.z));
    } else {
        const float inf = FLT_MAX;
        six->SetTranslationLimits(JPH::Vec3(-inf, -inf, -inf), JPH::Vec3(inf, inf, inf));
        six->SetRotationLimits(JPH::Vec3(-inf, -inf, -inf), JPH::Vec3(inf, inf, inf));
    }
    const float sf[3] = {j.softTransFreq.x, j.softTransFreq.y, j.softTransFreq.z};
    const float sd[3] = {j.softTransDamp.x, j.softTransDamp.y, j.softTransDamp.z};
    for (int i = 0; i < 3; ++i)
        six->SetLimitsSpringSettings(Axis(i), JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, sf[i], sd[i]));
    const float ft[3] = {j.frictionTrans.x, j.frictionTrans.y, j.frictionTrans.z};
    const float fa[3] = {j.frictionAng.x, j.frictionAng.y, j.frictionAng.z};
    for (int i = 0; i < 3; ++i) {
        six->SetMaxFriction(Axis(i), ft[i]);
        six->SetMaxFriction(Axis(i + 3), fa[i]);
    }
    const auto mv = [](const std::vector<float>& v, int i, float def) {
        return i < (int)v.size() ? v[i] : def;
    };
    const float mf[3] = {j.motorFreq.x, j.motorFreq.y, j.motorFreq.z};
    const float md[3] = {j.motorDamp.x, j.motorDamp.y, j.motorDamp.z};
    const float fmin[3] = {j.motorForceMin.x, j.motorForceMin.y, j.motorForceMin.z};
    const float fmax[3] = {j.motorForceMax.x, j.motorForceMax.y, j.motorForceMax.z};
    const float tmin[3] = {j.motorTorqueMin.x, j.motorTorqueMin.y, j.motorTorqueMin.z};
    const float tmax[3] = {j.motorTorqueMax.x, j.motorTorqueMax.y, j.motorTorqueMax.z};
    for (int i = 0; i < 6; ++i) {
        six->SetMotorState(Axis(i), JPH::EMotorState((int)mv(j.motorState, i, 0.f)));
        if (mv(j.motorState, i, 0.f) <= 0.f)
            continue;
        JPH::MotorSettings& ms = six->GetMotorSettings(Axis(i));
        ms.mSpringSettings = JPH::SpringSettings((JPH::ESpringMode)(int)mv(j.motorMode, i, 0.f), mf[i % 3], md[i % 3]);
        if (i < 3) {
            if (fmin[i] == 0.f && fmax[i] == 0.f)
                ms.SetForceLimits(-FLT_MAX, FLT_MAX);
            else
                ms.SetForceLimits(fmin[i], fmax[i]);
        } else {
            if (tmin[i - 3] == 0.f && tmax[i - 3] == 0.f)
                ms.SetTorqueLimits(-FLT_MAX, FLT_MAX);
            else
                ms.SetTorqueLimits(tmin[i - 3], tmax[i - 3]);
        }
    }
    // targets are in Jolt's (pos2 - pos1) space, negate to express body A's motion;
    // orientation target is the relative frame rotation, so conjugate
    six->SetTargetPositionCS(JPH::Vec3(-j.motorTargetPos.x, -j.motorTargetPos.y, -j.motorTargetPos.z));
    six->SetTargetVelocityCS(JPH::Vec3(-j.motorTargetVel.x, -j.motorTargetVel.y, -j.motorTargetVel.z));
    six->SetTargetAngularVelocityCS(JPH::Vec3(-j.motorTargetAngVel.x, -j.motorTargetAngVel.y, -j.motorTargetAngVel.z));
    six->SetTargetOrientationCS(JPH::Quat(j.motorTargetQuat.x, j.motorTargetQuat.y, j.motorTargetQuat.z,
                                        j.motorTargetQuat.w).Conjugated());
    six->SetConstraintPriority((unsigned)j.priority);
    six->SetNumVelocityStepsOverride((unsigned)j.velSteps);
    six->SetNumPositionStepsOverride((unsigned)j.posSteps);
    // forceSleep owns the enabled flag while it holds a body; don't fight it
    if (m_forceSleep.count(en.a) == 0 && m_forceSleep.count(en.b) == 0)
        six->SetEnabled(j.enabled);
    // param changes don't wake sleeping bodies; re-activate so live tuning takes effect now
    m_phys->GetBodyInterface().ActivateConstraint(six);
    en.applied = j;
}

void RigidScene::releaseJoint(int idx) {
    if (idx < 0 || idx >= (int)m_joints.size() || !m_joints[idx].alive)
        return;
    m_phys->RemoveConstraint(m_joints[idx].constraint);
    m_joints[idx].constraint = nullptr;
    m_joints[idx].alive = false;
}

// FNV-1a over colliders' entity + shape params + local transform; a change is structural (forces rebuild)
uint64_t RigidScene::shapeFingerprint(const entt::registry& reg, const std::vector<entt::entity>& cols) {
    uint64_t h = 14695981039346656037ull;
    const auto mix = [&h](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    };
    for (entt::entity c : cols) {
        mix(&c, sizeof(c));
        const auto* col = reg.try_get<Collider>(c);
        if (!col)
            continue;
        mix(&col->shape, sizeof(col->shape));
        if (const auto* cs = reg.try_get<CapsuleShape>(col->shape))
            mix(cs, sizeof(CapsuleShape));
        else if (const auto* bs = reg.try_get<BoxShape>(col->shape))
            mix(bs, sizeof(BoxShape));
        else if (const auto* tc = reg.try_get<TaperedCapsule>(col->shape))
            mix(tc, sizeof(TaperedCapsule));
        else if (const auto* ss = reg.try_get<SphereShape>(col->shape))
            mix(ss, sizeof(SphereShape));
        else if (const auto* ch = reg.try_get<ConvexHullShape>(col->shape)) {
            // vector field: hash the count + actual data (sizeof would hit the unstable ptr)
            const size_t n = ch->points.size();
            mix(&n, sizeof(size_t));
            if (!ch->points.empty())
                mix(ch->points.data(), n * sizeof(float));
        }
        const Transform t = reg.try_get<Transform>(c) ? reg.get<Transform>(c) : Transform{};
        mix(&t, sizeof(Transform));
    }
    return h;
}

RigidScene::Applied RigidScene::appliedFor(const RigidBody& rb, uint64_t shapeHash, bool kinematic) {
    Applied a;
    a.body = rb;
    a.shapeHash = shapeHash;
    a.kinematic = kinematic;
    return a;
}

// mass 0 = auto (the shape's own mass)
void RigidScene::syncScalars(entt::entity e, const RigidBody& rb) {
    const auto it = m_body.find(e);
    if (it == m_body.end())
        return;
    auto& bi = m_phys->GetBodyInterface();
    const JPH::BodyID id = it->second;
    bi.SetFriction(id, rb.friction);
    bi.SetRestitution(id, rb.restitution);
    JPH::BodyLockWrite lock(m_phys->GetBodyLockInterface(), id);
    JPH::Body& b = lock.GetBody();
    JPH::MotionProperties* mp = b.GetMotionProperties();
    mp->SetLinearDamping(rb.damping);
    JPH::MassProperties mpp = b.GetShape()->GetMassProperties();
    if (rb.mass > 0)
        mpp.ScaleToMass(rb.mass);
    mpp.mInertia(3, 3) = 1.0f;
    mp->SetMassProperties(mp->GetAllowedDOFs(), mpp);
}

void RigidScene::rebuildBody(entt::entity e, const entt::registry& reg, const RigidBody& rb, const Transform& t) {
    JPH::Vec3 linV, angV;
    bool hasVel = false;
    std::vector<entt::entity> joints;
    const auto it = m_body.find(e);
    if (it != m_body.end()) {
        joints = affectedJoints(it->second);
        JPH::BodyLockRead lock(m_phys->GetBodyLockInterface(), it->second);
        const JPH::Body& b = lock.GetBody();
        if (b.GetMotionProperties()) {
            linV = b.GetMotionProperties()->GetLinearVelocity();
            angV = b.GetMotionProperties()->GetAngularVelocity();
            hasVel = true;
        }
    }
    for (entt::entity c : rb.colliders)
        if (const auto* col = reg.try_get<Collider>(c))
            m_shape.erase(col->shape);
    removeBody(e);
    buildBody(e, colliderDescs(reg, rb.colliders), rb, t);
    if (hasVel && m_body.count(e)) {
        auto& bi = m_phys->GetBodyInterface();
        bi.SetLinearVelocity(m_body[e], linV);
        bi.SetAngularVelocity(m_body[e], angV);
    }
    rejoinJoints(joints, reg);
}

void RigidScene::rebuildKinematic(entt::entity e, const entt::registry& reg, const std::vector<entt::entity>& cols,
                                  const Transform& t) {
    std::vector<entt::entity> joints;
    const auto it = m_body.find(e);
    if (it != m_body.end())
        joints = affectedJoints(it->second);
    for (entt::entity c : cols)
        if (const auto* col = reg.try_get<Collider>(c))
            m_shape.erase(col->shape);
    removeBody(e);
    buildKinematic(e, colliderDescs(reg, cols), t);
    rejoinJoints(joints, reg);
}

std::vector<entt::entity> RigidScene::affectedJoints(JPH::BodyID id) const {
    std::vector<entt::entity> out;
    for (const auto& j : m_joints) {
        if (!j.alive)
            continue;
        auto* two = static_cast<const JPH::TwoBodyConstraint*>(j.constraint.GetPtr());
        if (two->GetBody1()->GetID() == id || two->GetBody2()->GetID() == id)
            out.push_back(j.ent);
    }
    return out;
}

void RigidScene::rejoinJoints(const std::vector<entt::entity>& list, const entt::registry& reg) {
    for (entt::entity je : list) {
        if (const auto* j = reg.try_get<Joint>(je)) {
            if (makeJoint(je, *j)) {
                m_jointIdx[je] = (int)m_joints.size() - 1;
                continue;
            }
        } else if (const auto* s = reg.try_get<SixDofJoint>(je)) {
            if (makeSixDofJoint(je, *s)) {
                m_jointIdx[je] = (int)m_joints.size() - 1;
                continue;
            }
        }
        m_jointIdx.erase(je);
    }
}

void RigidScene::project(const entt::registry& reg) {
    for (auto [e, rb] : reg.view<RigidBody>().each()) {
        if (reg.try_get<KinematicBody>(e))
            continue;
        const auto* t = reg.try_get<Transform>(e);
        if (!t)
            continue;
        const uint64_t sh = shapeFingerprint(reg, rb.colliders);
        const auto a = m_applied.find(e);
        const bool structural = a == m_applied.end() || a->second.kinematic || a->second.body.colliders != rb.colliders ||
                               a->second.shapeHash != sh;
        if (structural) {
            rebuildBody(e, reg, rb, *t);
            m_applied[e] = appliedFor(rb, sh, false);
        } else if (a->second.body != rb) {
            syncScalars(e, rb);
            Applied up = appliedFor(rb, sh, false);
            up.sleep = a->second.sleep;
            m_applied[e] = up;
        }
        // forceSleep true = hold asleep (welds off); false = one-shot release on the true->false edge
        const auto bt = m_body.find(e);
        if (rb.forceSleep) {
            m_forceSleep.insert(e);
            m_applied[e].sleep = 1;
            if (bt != m_body.end() && m_phys->GetBodyInterface().IsActive(bt->second))
                m_phys->GetBodyInterface().DeactivateBody(bt->second);
            for (auto& j : m_joints)
                if (j.alive && (j.a == e || j.b == e))
                    j.constraint->SetEnabled(false);
        } else {
            m_forceSleep.erase(e);
            if (m_applied[e].sleep == 1) {
                m_applied[e].sleep = 0;
                if (bt != m_body.end())
                    m_phys->GetBodyInterface().ActivateBody(bt->second);
                for (auto& j : m_joints)
                    if (j.alive && (j.a == e || j.b == e))
                        j.constraint->SetEnabled(true);
            }
        }
    }
    for (auto [e, kb] : reg.view<KinematicBody>().each()) {
        const auto* t = reg.try_get<Transform>(e);
        if (!t)
            continue;
        const auto a = m_applied.find(e);
        if (a != m_applied.end() && !a->second.kinematic)
            continue; // a RigidBody owns the Jolt body
        const uint64_t sh = shapeFingerprint(reg, kb.colliders);
        if (a != m_applied.end() && a->second.body.colliders == kb.colliders && a->second.shapeHash == sh)
            continue;
        RigidBody rec;
        rec.colliders = kb.colliders;
        rebuildKinematic(e, reg, kb.colliders, *t);
        m_applied[e] = appliedFor(rec, sh, true);
    }
    for (auto [e, j] : reg.view<Joint>().each()) {
        const auto it = m_jointIdx.find(e);
        if (it != m_jointIdx.end()) {
            syncJointExclusion(m_joints[it->second], j.excludeCollision);
            continue;
        }
        if (!makeJoint(e, j))
            continue;
        m_jointIdx[e] = (int)m_joints.size() - 1;
    }
    for (auto [e, j] : reg.view<SixDofJoint>().each()) {
        const auto it = m_jointIdx.find(e);
        if (it != m_jointIdx.end()) {
            JointEntry& en = m_joints[it->second];
            syncJointExclusion(en, j.excludeCollision);
            if (sixDofStructuralDiffers(en.applied, j)) {
                // frame-baked fields changed: rebuild the constraint (velocity lives on the bodies)
                releaseJoint(it->second);
                if (!makeSixDofJoint(e, j))
                    continue;
                setExcludes(j.bodyA, j.bodyB, j.excludeCollision); // makeSixDofJoint only ever adds excludes
                m_jointIdx[e] = (int)m_joints.size() - 1;
            } else if (sixDofDiffers(en.applied, j)) {
                syncSixDof(en, j);
            }
            continue;
        }
        if (!makeSixDofJoint(e, j))
            continue;
        m_jointIdx[e] = (int)m_joints.size() - 1;
    }
}

void RigidScene::step(entt::registry& reg, float dt) {
    int vel = 500, pos = 500;
    for (auto [e, s] : reg.view<PhysicsSettings>().each()) {
        vel = s.velocitySteps;
        pos = s.positionSteps;
        break;
    }
    m_settings.mNumVelocitySteps = (unsigned)vel;
    m_settings.mNumPositionSteps = (unsigned)pos;
    m_phys->SetPhysicsSettings(m_settings);
    // a constraint never wakes a sleeping body (Jolt only wakes on collision),
    // so a chain dragged by a moving body would leave its asleep tail behind
    auto& bi = m_phys->GetBodyInterface();
    for (auto& j : m_joints) {
        if (!j.alive || !j.constraint->GetEnabled())
            continue;
        auto* two = static_cast<JPH::TwoBodyConstraint*>(j.constraint.GetPtr());
        JPH::Body* a = two->GetBody1();
        JPH::Body* b = two->GetBody2();
        if (!a->GetMotionProperties() || !b->GetMotionProperties())
            continue;
        const float motion = a->GetLinearVelocity().LengthSq() + a->GetAngularVelocity().LengthSq()
                         + b->GetLinearVelocity().LengthSq() + b->GetAngularVelocity().LengthSq();
        if (motion < 1e-4f)
            continue;
        bi.ActivateConstraint(two);
    }
    // impulse is a one-shot command: applied, then cleared
    for (auto [e, rb] : reg.view<RigidBody>().each()) {
        if (!hasBody(e))
            continue;
        if (glm::length(rb.force) > 1e-9f)
            bi.AddForce(m_body[e], jVec(rb.force));
        if (glm::length(rb.impulse) > 1e-9f) {
            bi.AddImpulse(m_body[e], jVec(rb.impulse));
            reg.get<RigidBody>(e).impulse = {};
        }
    }
    m_phys->Update(dt, 1, m_temp, m_job);
}

Transform RigidScene::pose(entt::entity e) const {
    const auto it = m_body.find(e);
    if (it == m_body.end())
        return {};
    JPH::BodyLockRead lock(m_phys->GetBodyLockInterface(), it->second);
    const JPH::Body& b = lock.GetBody();
    const JPH::Vec3 p = b.GetPosition();
    const JPH::Quat q = b.GetRotation();
    return {{p.GetX(), p.GetY(), p.GetZ()}, {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}};
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
    const JointEntry en = m_joints[it->second];
    releaseJoint(it->second);
    m_jointIdx.erase(it);
    if (en.exclude)
        setExcludes(en.a, en.b, false);
}

bool RigidScene::excludes(entt::entity a, entt::entity b) const {
    const auto it = m_excludes.find(a);
    return it != m_excludes.end() && it->second.count(b) > 0;
}

void RigidScene::syncExcludes(entt::entity e) {
    const auto it = m_body.find(e);
    if (it == m_body.end())
        return;
    JPH::Body* body = m_phys->GetBodyLockInterface().TryGetBody(it->second);
    if (!body)
        return;
    const auto ex = m_excludes.find(e);
    const bool has = ex != m_excludes.end() && !ex->second.empty();
    JPH::CollisionGroup& g = body->GetCollisionGroup();
    if (has) {
        g.SetGroupFilter(m_excludeFilter.GetPtr());
        g.SetGroupID(entt::to_integral(e));
        g.SetSubGroupID(0);
    } else {
        g.SetGroupFilter(nullptr);
        g.SetGroupID(JPH::CollisionGroup::cInvalidGroup);
        g.SetSubGroupID(JPH::CollisionGroup::cInvalidSubGroup);
    }
}

void RigidScene::setExcludes(entt::entity a, entt::entity b, bool on) {
    if (a == entt::null || b == entt::null)
        return;
    if (on) {
        m_excludes[a].insert(b);
        m_excludes[b].insert(a);
    } else {
        m_excludes[a].erase(b);
        m_excludes[b].erase(a);
    }
    syncExcludes(a);
    syncExcludes(b);
}

void RigidScene::syncJointExclusion(JointEntry& en, bool on) {
    if (en.exclude == on || en.a == entt::null || en.b == entt::null)
        return;
    en.exclude = on;
    setExcludes(en.a, en.b, on);
}
