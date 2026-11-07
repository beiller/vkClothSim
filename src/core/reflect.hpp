#pragma once
#include "components.hpp"
#include <cstddef>
#include <cstring>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

enum class FieldKind {
    SliderF,
    SliderI,
    Checkbox,
    Color,
    DragV3,
    Ref,
    Str,
    StrArr,
    Quat,
    FloatArr,
    UIntArr,
    EntArr
};

struct FieldDesc {
    const char* name;
    ptrdiff_t offset;
    FieldKind kind;
    float lo = 0.0f, hi = 1.0f;
};

struct TypeDesc {
    const char* name;
    std::span<const FieldDesc> fields;
};

inline const FieldDesc* findField(const TypeDesc* t, const std::string& name) {
    for (const auto& f : t->fields)
        if (f.name == name)
            return &f;
    return nullptr;
}

namespace refl {

#define OFF(T, f) offsetof(T, f)

/// Transform | position/scale/rotation of an entity; `quat` is {x,y,z,w} and may also be written via qx..qw
static const FieldDesc kTransform[] = {
    {"pos", OFF(Transform, pos), FieldKind::DragV3},
    {"scale", OFF(Transform, scale), FieldKind::DragV3},
    {"quat", OFF(Transform, quat), FieldKind::Quat}, // 4-value array (the editor uses qx..qw)
    {"qx", OFF(Transform, quat), FieldKind::SliderF, -1.0f, 1.0f},
    {"qy", OFF(Transform, quat) + 4, FieldKind::SliderF, -1.0f, 1.0f},
    {"qz", OFF(Transform, quat) + 8, FieldKind::SliderF, -1.0f, 1.0f},
    {"qw", OFF(Transform, quat) + 12, FieldKind::SliderF, -1.0f, 1.0f},
};
/// Material | PBR material: baseColor/emissive (vec3), metallic/roughness [0,1], up to 4 texture refs (tex/texMR/texNormal/texEmissive), alphaMode 0=opaque 1=mask 2=blend, SSS and refraction
static const FieldDesc kMaterial[] = {
    {"baseColor", OFF(Material, baseColor), FieldKind::Color},
    {"metallic", OFF(Material, metallic), FieldKind::SliderF, 0.0f, 1.0f},
    {"roughness", OFF(Material, roughness), FieldKind::SliderF, 0.0f, 1.0f},
    {"emissive", OFF(Material, emissive), FieldKind::Color},
    {"tex", OFF(Material, tex), FieldKind::Ref},
    {"texMR", OFF(Material, texMR), FieldKind::Ref},
    {"texNormal", OFF(Material, texNormal), FieldKind::Ref},
    {"texEmissive", OFF(Material, texEmissive), FieldKind::Ref},
    {"alphaMode", OFF(Material, alphaMode), FieldKind::SliderI, 0.0f, 2.0f},
    {"alphaCutoff", OFF(Material, alphaCutoff), FieldKind::SliderF, 0.0f, 1.0f},
    {"alpha", OFF(Material, alpha), FieldKind::SliderF, 0.0f, 1.0f},
    {"ssssWeight", OFF(Material, ssssWeight), FieldKind::Color},
    {"refraction", OFF(Material, refraction), FieldKind::SliderF, -1.0f, 1.0f},
    {"ior", OFF(Material, ior), FieldKind::SliderF, 1.0f, 1.8f},
    {"normalScale", OFF(Material, normalScale), FieldKind::SliderF, 0.0f, 3.0f},
    {"ssssScale", OFF(Material, ssssScale), FieldKind::SliderF, 0.0f, 1.0f},
    {"ssssSplit", OFF(Material, ssssSplit), FieldKind::SliderF, 0.0f, 1.0f},
};
/// MaterialRef | points an entity at the Material entity used for its rendering (`material` = material entity)
static const FieldDesc kMaterialRef[] = {
    {"material", 0, FieldKind::Ref},
};
/// Parent | scene hierarchy: `e` = parent entity
static const FieldDesc kParent[] = {
    {"e", OFF(Parent, e), FieldKind::Ref},
};
/// Camera | camera: fovDeg/nearP/farP; `active=true` makes it THE camera (first active wins); XR hand-off deactivates all non-VR cameras while the headset has a pose
static const FieldDesc kCamera[] = {
    {"fovDeg", OFF(Camera, fovDeg), FieldKind::SliderF, 10.0f, 120.0f},
    {"nearP", OFF(Camera, nearP), FieldKind::SliderF, 0.01f, 5.0f},
    {"farP", OFF(Camera, farP), FieldKind::SliderF, 10.0f, 1000.0f},
    {"active", OFF(Camera, active), FieldKind::Checkbox},
};
/// Environment | environment lighting: intensity + environment texture ref (`tex`)
static const FieldDesc kEnvironment[] = {
    {"intensity", OFF(Environment, intensity), FieldKind::SliderF, 0.0f, 10.0f},
    {"tex", OFF(Environment, tex), FieldKind::Ref},
};
/// PointLight | point light: color (vec3), intensity, radius (falloff), on 0/1, shadow tuning fields
static const FieldDesc kPointLight[] = {
    {"on", OFF(PointLight, on), FieldKind::SliderF, 0.0f, 1.0f},
    {"color", OFF(PointLight, color), FieldKind::Color},
    {"intensity", OFF(PointLight, intensity), FieldKind::SliderF, 0.0f, 1000.0f},
    {"radius", OFF(PointLight, radius), FieldKind::SliderF, 0.0f, 2.0f},
    {"shadowNear", OFF(PointLight, shadowNear), FieldKind::SliderF, 0.001f, 1.0f},
    {"shadowFar", OFF(PointLight, shadowFar), FieldKind::SliderF, 1.0f, 100.0f},
    {"shadowNormalBias", OFF(PointLight, shadowNormalBias), FieldKind::SliderF, 0.0f, 1.0f},
    {"shadowBiasBase", OFF(PointLight, shadowBiasBase), FieldKind::SliderF, 0.0f, 2.0f},
    {"shadowBiasSlope", OFF(PointLight, shadowBiasSlope), FieldKind::SliderF, 0.0f, 4.0f},
    {"shadowSearchScale", OFF(PointLight, shadowSearchScale), FieldKind::SliderF, 0.5f, 16.0f},
    {"shadowMaxRadius", OFF(PointLight, shadowMaxRadius), FieldKind::SliderF, 1.0f, 64.0f},
};
/// SoftBodyData | GPU PBD soft-body params: pinned (0=free, >0=pinned), solver steps/passes, mass/damping/stiffness/tension/friction, cons = constraint param array
static const FieldDesc kSoftBodyData[] = {
    {"pinned", OFF(SoftBodyData, pinned), FieldKind::SliderI, 0.0f, 2.0f},
    {"steps", OFF(SoftBodyData, steps), FieldKind::SliderI, 1.0f, 32.0f},
    {"mass", OFF(SoftBodyData, mass), FieldKind::SliderF, 0.1f, 10.0f},
    {"damping", OFF(SoftBodyData, damping), FieldKind::SliderF, 0.9f, 1.0f},
    {"passes", OFF(SoftBodyData, passes), FieldKind::SliderI, 1.0f, 16.0f},
    {"stiffness", OFF(SoftBodyData, stiffness), FieldKind::SliderF, 0.0f, 1.0f},
    {"tension", OFF(SoftBodyData, tension), FieldKind::SliderF, 0.5f, 1.5f},
    {"friction", OFF(SoftBodyData, friction), FieldKind::SliderF, 0.0f, 50.0f},
    {"cons", OFF(SoftBodyData, cons), FieldKind::FloatArr},
};
/// RigidBody | Jolt dynamic body: friction/restitution/damping/mass, forceSleep, `colliders` = collider entities
static const FieldDesc kRigidBody[] = {
    {"friction", OFF(RigidBody, friction), FieldKind::SliderF, 0.0f, 1.0f},
    {"restitution", OFF(RigidBody, restitution), FieldKind::SliderF, 0.0f, 1.0f},
    {"damping", OFF(RigidBody, damping), FieldKind::SliderF, 0.0f, 1.0f},
    {"mass", OFF(RigidBody, mass), FieldKind::SliderF, 0.0f, 1000.0f},
    {"forceSleep", OFF(RigidBody, forceSleep), FieldKind::Checkbox},
    {"colliders", OFF(RigidBody, colliders), FieldKind::EntArr},
    {"force", OFF(RigidBody, force), FieldKind::DragV3},
    {"impulse", OFF(RigidBody, impulse), FieldKind::DragV3},
};
/// KinematicBody | Jolt kinematic (script-driven) body: `colliders` = collider entities
static const FieldDesc kKinematicBody[] = {
    {"colliders", OFF(KinematicBody, colliders), FieldKind::EntArr},
};
/// CapsuleShape | capsule collider shape: halfLen + radius
static const FieldDesc kCapsuleShape[] = {
    {"halfLen", OFF(CapsuleShape, halfLen), FieldKind::SliderF, 0.0f, 10.0f},
    {"radius", OFF(CapsuleShape, radius), FieldKind::SliderF, 0.0f, 5.0f},
};
/// BoxShape | box collider shape: `half` = {hx,hy,hz}, may also be written via hx..hz
static const FieldDesc kBoxShape[] = {
    {"half", OFF(BoxShape, half), FieldKind::DragV3}, // 3-value array (the editor uses hx..hz)
    {"hx", OFF(BoxShape, half), FieldKind::SliderF, 0.0f, 100.0f},
    {"hy", OFF(BoxShape, half) + 4, FieldKind::SliderF, 0.0f, 100.0f},
    {"hz", OFF(BoxShape, half) + 8, FieldKind::SliderF, 0.0f, 100.0f},
};
/// TaperedCapsule | tapered-capsule collider shape: halfHeight + bottom/top radius
static const FieldDesc kTaperedCapsule[] = {
    {"halfHeight", OFF(TaperedCapsule, halfHeight), FieldKind::SliderF, 0.0f, 10.0f},
    {"bottomRadius", OFF(TaperedCapsule, bottomRadius), FieldKind::SliderF, 0.0f, 5.0f},
    {"topRadius", OFF(TaperedCapsule, topRadius), FieldKind::SliderF, 0.0f, 5.0f},
};

/// SphereShape | sphere collider shape: radius
static const FieldDesc kSphereShape[] = {
    {"radius", OFF(SphereShape, radius), FieldKind::SliderF, 0.0f, 10.0f},
};
/// ConvexHullShape | convex-hull collider shape: `points` = flat xyz triples of the hull vertices
static const FieldDesc kConvexHullShape[] = {
    {"points", OFF(ConvexHullShape, points), FieldKind::FloatArr},
};
/// Collider | links a shape entity to a body: `shape` = shape entity ref
static const FieldDesc kCollider[] = {
    {"shape", OFF(Collider, shape), FieldKind::Ref},
};
/// Joint | physics joint between two bodies (bodyA/bodyB refs); type (only 0 = point is supported); grabPos; excludeCollision suppresses collision between the joined bodies
static const FieldDesc kJoint[] = {
    {"bodyA", OFF(Joint, bodyA), FieldKind::Ref},
    {"bodyB", OFF(Joint, bodyB), FieldKind::Ref},
    {"type", OFF(Joint, type), FieldKind::SliderI, 0.0f, 1.0f},
    {"grabPos", OFF(Joint, grabPos), FieldKind::DragV3},
    {"excludeCollision", OFF(Joint, excludeCollision), FieldKind::Checkbox},
};
/// SixDofJoint | 6-DOF joint: bodyA/bodyB refs (null bodyB = fixed to world), translation/angle limits (lim*), soft limits, per-axis motors (motor*), priority/solver steps, enabled
static const FieldDesc kSixDofJoint[] = {
    {"bodyA", OFF(SixDofJoint, bodyA), FieldKind::Ref},
    {"bodyB", OFF(SixDofJoint, bodyB), FieldKind::Ref},
    {"grabPos", OFF(SixDofJoint, grabPos), FieldKind::DragV3},
    {"worldSpace", OFF(SixDofJoint, worldSpace), FieldKind::Checkbox},
    {"relQuat", OFF(SixDofJoint, relQuat), FieldKind::Quat},
    {"limOn", OFF(SixDofJoint, limOn), FieldKind::Checkbox},
    {"limTransMin", OFF(SixDofJoint, limTransMin), FieldKind::DragV3},
    {"limTransMax", OFF(SixDofJoint, limTransMax), FieldKind::DragV3},
    {"limAngMin", OFF(SixDofJoint, limAngMin), FieldKind::DragV3},
    {"limAngMax", OFF(SixDofJoint, limAngMax), FieldKind::DragV3},
    {"softTransFreq", OFF(SixDofJoint, softTransFreq), FieldKind::DragV3},
    {"softTransDamp", OFF(SixDofJoint, softTransDamp), FieldKind::DragV3},
    {"pyramid", OFF(SixDofJoint, pyramid), FieldKind::Checkbox},
    {"frictionTrans", OFF(SixDofJoint, frictionTrans), FieldKind::DragV3},
    {"frictionAng", OFF(SixDofJoint, frictionAng), FieldKind::DragV3},
    {"motorState", OFF(SixDofJoint, motorState), FieldKind::FloatArr},
    {"motorMode", OFF(SixDofJoint, motorMode), FieldKind::FloatArr},
    {"motorFreq", OFF(SixDofJoint, motorFreq), FieldKind::DragV3},
    {"motorDamp", OFF(SixDofJoint, motorDamp), FieldKind::DragV3},
    {"motorForceMin", OFF(SixDofJoint, motorForceMin), FieldKind::DragV3},
    {"motorForceMax", OFF(SixDofJoint, motorForceMax), FieldKind::DragV3},
    {"motorTorqueMin", OFF(SixDofJoint, motorTorqueMin), FieldKind::DragV3},
    {"motorTorqueMax", OFF(SixDofJoint, motorTorqueMax), FieldKind::DragV3},
    {"motorTargetPos", OFF(SixDofJoint, motorTargetPos), FieldKind::DragV3},
    {"motorTargetVel", OFF(SixDofJoint, motorTargetVel), FieldKind::DragV3},
    {"motorTargetAngVel", OFF(SixDofJoint, motorTargetAngVel), FieldKind::DragV3},
    {"motorTargetQuat", OFF(SixDofJoint, motorTargetQuat), FieldKind::Quat},
    {"priority", OFF(SixDofJoint, priority), FieldKind::SliderI, 0.0f, 1023.0f},
    {"velSteps", OFF(SixDofJoint, velSteps), FieldKind::SliderI, 0.0f, 64.0f},
    {"posSteps", OFF(SixDofJoint, posSteps), FieldKind::SliderI, 0.0f, 64.0f},
    {"enabled", OFF(SixDofJoint, enabled), FieldKind::Checkbox},
    {"excludeCollision", OFF(SixDofJoint, excludeCollision), FieldKind::Checkbox},
};
/// ControllerInput | XR controller state: trigger_0/1, axis_0/1, button_0/1, active, aim pose (aim_pos + aim_qx..qw)
static const FieldDesc kControllerInput[] = {
    {"trigger_0", OFF(ControllerInput, triggers), FieldKind::SliderF, 0.0f, 1.0f},
    {"trigger_1", OFF(ControllerInput, triggers) + 4, FieldKind::SliderF, 0.0f, 1.0f},
    {"axis_0", OFF(ControllerInput, axis), FieldKind::SliderF, -1.0f, 1.0f},
    {"axis_1", OFF(ControllerInput, axis) + 4, FieldKind::SliderF, -1.0f, 1.0f},
    {"button_0", OFF(ControllerInput, buttons), FieldKind::Checkbox},
    {"button_1", OFF(ControllerInput, buttons) + 1, FieldKind::Checkbox},
    {"active", OFF(ControllerInput, active), FieldKind::Checkbox},
    {"aim_pos", OFF(ControllerInput, aimPos), FieldKind::DragV3},
    {"aim_qx", OFF(ControllerInput, aimQuat), FieldKind::SliderF, -1.0f, 1.0f},
    {"aim_qy", OFF(ControllerInput, aimQuat) + 4, FieldKind::SliderF, -1.0f, 1.0f},
    {"aim_qz", OFF(ControllerInput, aimQuat) + 8, FieldKind::SliderF, -1.0f, 1.0f},
    {"aim_qw", OFF(ControllerInput, aimQuat) + 12, FieldKind::SliderF, -1.0f, 1.0f},
};
/// MouseInput | mouse state: x/y position, button_0..3
static const FieldDesc kMouseInput[] = {
    {"x", OFF(MouseInput, x), FieldKind::SliderF, 0.0f, 3840.0f},
    {"y", OFF(MouseInput, y), FieldKind::SliderF, 0.0f, 2160.0f},
    {"button_0", OFF(MouseInput, buttons), FieldKind::Checkbox},
    {"button_1", OFF(MouseInput, buttons) + 1, FieldKind::Checkbox},
    {"button_2", OFF(MouseInput, buttons) + 2, FieldKind::Checkbox},
    {"button_3", OFF(MouseInput, buttons) + 3, FieldKind::Checkbox},
};
/// Renderable | makes an entity renderable: `geom` = geometry entity ref, noShadow, visible
static const FieldDesc kRenderable[] = {
    {"geom", 0, FieldKind::Ref},
    {"noShadow", OFF(Renderable, noShadow), FieldKind::Checkbox},
    {"visible", OFF(Renderable, visible), FieldKind::Checkbox},
};
/// Texture | texture from a file: `path`, `space` = "linear" or "srgb" (sRGB data is decoded on upload)
static const FieldDesc kTexture[] = {
    {"path", OFF(Texture, path), FieldKind::Str},
    {"space", OFF(Texture, space), FieldKind::Str},
};
/// Geometry | mesh data: pos/nrm/uv float arrays, idx/joints int arrays, weights float array (per-vertex skin weights)
static const FieldDesc kGeometry[] = {
    {"pos", OFF(Geometry, mesh.pos), FieldKind::FloatArr},
    {"nrm", OFF(Geometry, mesh.nrm), FieldKind::FloatArr},
    {"uv", OFF(Geometry, mesh.uv), FieldKind::FloatArr},
    {"idx", OFF(Geometry, mesh.indices), FieldKind::UIntArr},
    {"joints", OFF(Geometry, mesh.joints), FieldKind::UIntArr},
    {"weights", OFF(Geometry, mesh.weights), FieldKind::FloatArr},
};
// invBinds is not a field: it is derived (the inverse of each joint's world
// transform) whenever the component is written through createOrReplace
/// Skeleton | skinning skeleton: name, boneNames string array, joints entity array, placement ref, meshes entity array; invBinds is derived on write
static const FieldDesc kSkeleton[] = {
    {"name", OFF(Skeleton, name), FieldKind::Str},
    {"boneNames", OFF(Skeleton, boneNames), FieldKind::StrArr},
    {"joints", OFF(Skeleton, joints), FieldKind::EntArr},
    {"placement", OFF(Skeleton, placement), FieldKind::Ref},
    {"meshes", OFF(Skeleton, meshes), FieldKind::EntArr},
};
/// RenderSettings | global render settings (emplace on a marker entity): bgColor (vec3), exposure, sssDepthReject, sssCurvScale
static const FieldDesc kRenderSettings[] = {
    {"bgColor", OFF(RenderSettings, bgColor), FieldKind::Color},
    {"exposure", OFF(RenderSettings, exposure), FieldKind::SliderF, 0.1f, 3.0f},
    {"sssDepthReject", OFF(RenderSettings, sssDepthReject), FieldKind::SliderF, 0.0f, 8.0f},
    {"sssCurvScale", OFF(RenderSettings, sssCurvScale), FieldKind::SliderF, 0.0f, 10.0f},
};
/// PhysicsSettings | global physics solver steps (emplace on a marker entity): velocitySteps/positionSteps
static const FieldDesc kPhysicsSettings[] = {
    {"velocitySteps", OFF(PhysicsSettings, velocitySteps), FieldKind::SliderI, 1.0f, 512.0f},
    {"positionSteps", OFF(PhysicsSettings, positionSteps), FieldKind::SliderI, 1.0f, 512.0f},
};

/// Bind | per-frame URL copy: source `url` -> `target` field (empty = widget value); `dir` 0=Both 1=Read 2=Write
static const FieldDesc kBind[] = {
    {"url", OFF(Bind, url), FieldKind::Str},
    {"target", OFF(Bind, target), FieldKind::Str},
    {"dir", OFF(Bind, dir), FieldKind::SliderI, 0.0f, 2.0f},
};

static const TypeDesc kTypes[] = {
    {"Transform", kTransform},
    {"Bind", kBind},
    {"Material", kMaterial},
    {"MaterialRef", kMaterialRef},
    {"Parent", kParent},
    {"Camera", kCamera},
    {"Environment", kEnvironment},
    {"PointLight", kPointLight},
    {"SoftBodyData", kSoftBodyData},
    {"RigidBody", kRigidBody},
    {"KinematicBody", kKinematicBody},
    {"CapsuleShape", kCapsuleShape},
    {"BoxShape", kBoxShape},
    {"TaperedCapsule", kTaperedCapsule},
    {"SphereShape", kSphereShape},
    {"ConvexHullShape", kConvexHullShape},
    {"Collider", kCollider},
    {"Joint", kJoint},
    {"SixDofJoint", kSixDofJoint},
    {"ControllerInput", kControllerInput},
    {"MouseInput", kMouseInput},
    {"Renderable", kRenderable},
    {"Texture", kTexture},
    {"Geometry", kGeometry},
    {"Skeleton", kSkeleton},
    {"RenderSettings", kRenderSettings},
    {"PhysicsSettings", kPhysicsSettings},
};

inline const TypeDesc* findType(const char* name) {
    for (const auto& t : kTypes)
        if (std::strcmp(t.name, name) == 0)
            return &t;
    return nullptr;
}

// live per-use pointer to a component: entt storage can move (the pool reallocates
// when new entities join, e.g. a glb load) and components can be re-emplaced, so
// callers resolve on every draw instead of capturing once
template <typename T>
std::function<const void*(void)> liveRef(const entt::registry& reg, entt::entity e) {
    return [&reg, e] -> const void* {
        if (auto* p = reg.try_get<T>(e))
            return (const void*)p;
        return nullptr;
    };
}

inline std::vector<std::pair<const TypeDesc*, std::function<const void*(void)>>> entityFields(const entt::registry& reg, entt::entity e) {
    std::vector<std::pair<const TypeDesc*, std::function<const void*(void)>>> out;
    auto add = [&out](const char* tn, std::function<const void*(void)> fn) {
        out.emplace_back(findType(tn), std::move(fn));
    };
    if (reg.try_get<Transform>(e)) add("Transform", liveRef<Transform>(reg, e));
    if (reg.try_get<Material>(e)) add("Material", liveRef<Material>(reg, e));
    if (reg.try_get<MaterialRef>(e)) add("MaterialRef", liveRef<MaterialRef>(reg, e));
    if (reg.try_get<Parent>(e)) add("Parent", liveRef<Parent>(reg, e));
    if (reg.try_get<Camera>(e)) add("Camera", liveRef<Camera>(reg, e));
    if (reg.try_get<Environment>(e)) add("Environment", liveRef<Environment>(reg, e));
    if (reg.try_get<PointLight>(e)) add("PointLight", liveRef<PointLight>(reg, e));
    if (reg.try_get<SoftBodyData>(e)) add("SoftBodyData", liveRef<SoftBodyData>(reg, e));
    if (reg.try_get<RigidBody>(e)) add("RigidBody", liveRef<RigidBody>(reg, e));
    if (reg.try_get<KinematicBody>(e)) add("KinematicBody", liveRef<KinematicBody>(reg, e));
    if (reg.try_get<CapsuleShape>(e)) add("CapsuleShape", liveRef<CapsuleShape>(reg, e));
    if (reg.try_get<BoxShape>(e)) add("BoxShape", liveRef<BoxShape>(reg, e));
    if (reg.try_get<TaperedCapsule>(e)) add("TaperedCapsule", liveRef<TaperedCapsule>(reg, e));
    if (reg.try_get<SphereShape>(e)) add("SphereShape", liveRef<SphereShape>(reg, e));
    if (reg.try_get<ConvexHullShape>(e)) add("ConvexHullShape", liveRef<ConvexHullShape>(reg, e));
    if (reg.try_get<Collider>(e)) add("Collider", liveRef<Collider>(reg, e));
    if (reg.try_get<Joint>(e)) add("Joint", liveRef<Joint>(reg, e));
    if (reg.try_get<SixDofJoint>(e)) add("SixDofJoint", liveRef<SixDofJoint>(reg, e));
    if (reg.try_get<ControllerInput>(e)) add("ControllerInput", liveRef<ControllerInput>(reg, e));
    if (reg.try_get<Renderable>(e)) add("Renderable", liveRef<Renderable>(reg, e));
    if (reg.try_get<Texture>(e)) add("Texture", liveRef<Texture>(reg, e));
    if (reg.try_get<Geometry>(e)) add("Geometry", liveRef<Geometry>(reg, e));
    if (reg.try_get<Skeleton>(e)) add("Skeleton", liveRef<Skeleton>(reg, e));
    if (reg.try_get<MouseInput>(e)) add("MouseInput", liveRef<MouseInput>(reg, e));
    if (reg.try_get<RenderSettings>(e)) add("RenderSettings", liveRef<RenderSettings>(reg, e));
    if (reg.try_get<PhysicsSettings>(e)) add("PhysicsSettings", liveRef<PhysicsSettings>(reg, e));
    if (reg.try_get<Bind>(e)) add("Bind", liveRef<Bind>(reg, e));
    return out;
}

}
