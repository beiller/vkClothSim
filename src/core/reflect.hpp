#pragma once
#include "components.hpp"
#include <cstddef>
#include <cstring>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

enum class FieldKind : uint8_t {
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
    /// local position relative to the parent (meters); set via scene.set_pose or drag in the editor
    {"pos", OFF(Transform, pos), FieldKind::DragV3},
    /// local scale multiplier per axis (1 = unchanged)
    {"scale", OFF(Transform, scale), FieldKind::DragV3},
    /// local rotation {x,y,z,w} (w last; identity = {0,0,0,1})
    {"quat", OFF(Transform, quat), FieldKind::Quat},
    /// slider alias for quat.x
    {"qx", OFF(Transform, quat), FieldKind::SliderF, -1.0f, 1.0f},
    /// slider alias for quat.y
    {"qy", OFF(Transform, quat) + 4, FieldKind::SliderF, -1.0f, 1.0f},
    /// slider alias for quat.z
    {"qz", OFF(Transform, quat) + 8, FieldKind::SliderF, -1.0f, 1.0f},
    /// slider alias for quat.w
    {"qw", OFF(Transform, quat) + 12, FieldKind::SliderF, -1.0f, 1.0f},
};
/// Material | PBR material: baseColor/emissive (vec3), metallic/roughness [0,1], up to 4 texture refs
/// (tex/texMR/texNormal/texEmissive), alphaMode 0=opaque 1=mask 2=blend, SSS and refraction
static const FieldDesc kMaterial[] = {
    /// albedo RGB (0..1); multiplied with tex when present
    {"baseColor", OFF(Material, baseColor), FieldKind::Color},
    /// metalness 0..1 (1 = pure metal, no diffuse lobe)
    {"metallic", OFF(Material, metallic), FieldKind::SliderF, 0.0f, 1.0f},
    /// surface roughness 0..1 (0 = mirror-like)
    {"roughness", OFF(Material, roughness), FieldKind::SliderF, 0.0f, 1.0f},
    /// unlit HDR emission added to the color; multiplied by texEmissive when present
    {"emissive", OFF(Material, emissive), FieldKind::Color},
    /// base-color texture entity (Texture or GPUTexture)
    {"tex", OFF(Material, tex), FieldKind::Ref},
    /// glTF metallicRoughness texture (G = roughness, B = metallic)
    {"texMR", OFF(Material, texMR), FieldKind::Ref},
    /// normal map; strength scaled by normalScale
    {"texNormal", OFF(Material, texNormal), FieldKind::Ref},
    /// emissive texture (multiplied with emissive)
    {"texEmissive", OFF(Material, texEmissive), FieldKind::Ref},
    /// 0 = opaque, 1 = mask (discard where alpha < alphaCutoff), 2 = blend
    {"alphaMode", OFF(Material, alphaMode), FieldKind::SliderI, 0.0f, 2.0f},
    /// discard threshold in mask mode
    {"alphaCutoff", OFF(Material, alphaCutoff), FieldKind::SliderF, 0.0f, 1.0f},
    /// constant opacity multiplier (mask/blend)
    {"alpha", OFF(Material, alpha), FieldKind::SliderF, 0.0f, 1.0f},
    /// per-channel subsurface-scatter blur distance (R/G/B); 0 = off; scaled by ssssScale
    {"ssssWeight", OFF(Material, ssssWeight), FieldKind::Color},
    /// glass amount 0..1 (>0 samples the scene color through Snell's law at ior)
    {"refraction", OFF(Material, refraction), FieldKind::SliderF, -1.0f, 1.0f},
    /// index of refraction for the glass bend (1.5 ~ glass)
    {"ior", OFF(Material, ior), FieldKind::SliderF, 1.0f, 1.8f},
    /// normal-map strength multiplier
    {"normalScale", OFF(Material, normalScale), FieldKind::SliderF, 0.0f, 3.0f},
    /// overall SSS blur-distance scale
    {"ssssScale", OFF(Material, ssssScale), FieldKind::SliderF, 0.0f, 1.0f},
    /// fraction of the albedo routed to the blurred (pre-scatter) half, 0..1
    {"ssssSplit", OFF(Material, ssssSplit), FieldKind::SliderF, 0.0f, 1.0f},
};
/// MaterialRef | points an entity at the Material entity used for its rendering (`material` = material entity)
static const FieldDesc kMaterialRef[] = {
    /// the Material entity this entity renders with
    {"material", 0, FieldKind::Ref},
};
/// Parent | scene hierarchy: `e` = parent entity
static const FieldDesc kParent[] = {
    /// the parent entity; the local Transform is relative to it
    {"e", OFF(Parent, e), FieldKind::Ref},
};
/// Camera | camera: fovDeg/nearP/farP; `active=true` makes it THE camera (first active wins); XR hand-off deactivates
/// all non-VR cameras while the headset has a pose
static const FieldDesc kCamera[] = {
    /// vertical field of view in degrees
    {"fovDeg", OFF(Camera, fovDeg), FieldKind::SliderF, 10.0f, 120.0f},
    /// near clip plane (meters)
    {"nearP", OFF(Camera, nearP), FieldKind::SliderF, 0.01f, 5.0f},
    /// far clip plane (meters)
    {"farP", OFF(Camera, farP), FieldKind::SliderF, 10.0f, 1000.0f},
    /// true makes this THE camera (first active wins); XR hand-off deactivates non-VR cameras
    {"active", OFF(Camera, active), FieldKind::Checkbox},
};
/// Environment | environment lighting: intensity + environment texture ref (`tex`)
static const FieldDesc kEnvironment[] = {
    /// environment-light multiplier
    {"intensity", OFF(Environment, intensity), FieldKind::SliderF, 0.0f, 10.0f},
    /// HDRI texture entity for image-based lighting and reflections
    {"tex", OFF(Environment, tex), FieldKind::Ref},
};
/// PointLight | point light: color (vec3), intensity, radius (falloff), on 0/1, shadow tuning fields
static const FieldDesc kPointLight[] = {
    /// 0/1 switch
    {"on", OFF(PointLight, on), FieldKind::SliderF, 0.0f, 1.0f},
    /// RGB tint of the light
    {"color", OFF(PointLight, color), FieldKind::Color},
    /// radiometric power (higher = brighter)
    {"intensity", OFF(PointLight, intensity), FieldKind::SliderF, 0.0f, 1000.0f},
    /// sphere-light physical radius (meters): sets specular/penumbra softness; falloff is inverse-square
    {"radius", OFF(PointLight, radius), FieldKind::SliderF, 0.0f, 2.0f},
    /// shadow-map near clip (meters)
    {"shadowNear", OFF(PointLight, shadowNear), FieldKind::SliderF, 0.001f, 1.0f},
    /// shadow-map far clip (meters)
    {"shadowFar", OFF(PointLight, shadowFar), FieldKind::SliderF, 1.0f, 100.0f},
    /// shadow acne tuning: push samples along the normal
    {"shadowNormalBias", OFF(PointLight, shadowNormalBias), FieldKind::SliderF, 0.0f, 1.0f},
    /// shadow acne tuning: constant depth bias
    {"shadowBiasBase", OFF(PointLight, shadowBiasBase), FieldKind::SliderF, 0.0f, 2.0f},
    /// shadow acne tuning: slope-scaled depth bias
    {"shadowBiasSlope", OFF(PointLight, shadowBiasSlope), FieldKind::SliderF, 0.0f, 4.0f},
    /// contact-hardening search scale (higher = tighter penumbra)
    {"shadowSearchScale", OFF(PointLight, shadowSearchScale), FieldKind::SliderF, 0.5f, 16.0f},
    /// maximum penumbra radius in the shadow map
    {"shadowMaxRadius", OFF(PointLight, shadowMaxRadius), FieldKind::SliderF, 1.0f, 64.0f},
};
/// SoftBodyData | GPU PBD soft-body params: pinned (0=free, >0=pinned), solver steps/passes,
/// mass/damping/stiffness/tension/friction, cons = constraint param array, per-vertex pinning (pinW/pinMap/pinSrc)
static const FieldDesc kSoftBodyData[] = {
    /// 0 = free, >0 = held in place (e.g. a fixed cloth anchor)
    {"pinned", OFF(SoftBodyData, pinned), FieldKind::SliderI, 0.0f, 2.0f},
    /// substeps per frame (dt is divided by this; damping is applied per substep)
    {"steps", OFF(SoftBodyData, steps), FieldKind::SliderI, 1.0f, 32.0f},
    /// body mass (kg)
    {"mass", OFF(SoftBodyData, mass), FieldKind::SliderF, 0.1f, 10.0f},
    /// velocity retention per frame (1 = no damping; applied as a per-substep factor)
    {"damping", OFF(SoftBodyData, damping), FieldKind::SliderF, 0.9f, 1.0f},
    /// PBD constraint-solver iterations per substep
    {"passes", OFF(SoftBodyData, passes), FieldKind::SliderI, 1.0f, 16.0f},
    /// constraint stiffness (higher = less compliant; alpha = 10/stiff)
    {"stiffness", OFF(SoftBodyData, stiffness), FieldKind::SliderF, 0.0f, 1.0f},
    /// global rest-length scale for all constraints (1 = as built; >1 stretches the cloth)
    {"tension", OFF(SoftBodyData, tension), FieldKind::SliderF, 0.5f, 1.5f},
    /// contact friction: caps tangential slip per contact correction
    {"friction", OFF(SoftBodyData, friction), FieldKind::SliderF, 0.0f, 50.0f},
    /// distance constraints as flat {a, b, rest, k} quads (vertex indices, rest length, stiffness)
    {"cons", OFF(SoftBodyData, cons), FieldKind::FloatArr},
    /// per-vertex pin weight in [0,1], one entry per vertex (0 = free); pulls the vertex toward its pin target with step-independent stiffness (1 = hard pin)
    {"pinW", OFF(SoftBodyData, pinW), FieldKind::FloatArr},
    /// optional target remap: cloth vertex i takes its pin target from source vertex pinMap[i] (default identity)
    {"pinMap", OFF(SoftBodyData, pinMap), FieldKind::UIntArr},
    /// pin target source mesh entity (null = the body's own current positions); refreshed by soft.update_pins
    {"pinSrc", OFF(SoftBodyData, pinSrc), FieldKind::Ref},
};
/// RigidBody | Jolt dynamic body: friction/restitution/damping/mass, forceSleep, `colliders` = collider entities
static const FieldDesc kRigidBody[] = {
    /// Coulomb friction coefficient 0..1
    {"friction", OFF(RigidBody, friction), FieldKind::SliderF, 0.0f, 1.0f},
    /// bounciness 0..1 (1 = perfectly elastic)
    {"restitution", OFF(RigidBody, restitution), FieldKind::SliderF, 0.0f, 1.0f},
    /// linear velocity damping 0..1
    {"damping", OFF(RigidBody, damping), FieldKind::SliderF, 0.0f, 1.0f},
    /// mass in kg (0 = static body)
    {"mass", OFF(RigidBody, mass), FieldKind::SliderF, 0.0f, 1000.0f},
    /// force the body to sleep immediately
    {"forceSleep", OFF(RigidBody, forceSleep), FieldKind::Checkbox},
    /// collider entities attached to this body
    {"colliders", OFF(RigidBody, colliders), FieldKind::EntArr},
    /// persistent external force (N), applied every physics step until changed
    {"force", OFF(RigidBody, force), FieldKind::DragV3},
    /// one-shot impulse (N*s), applied on the next step then cleared to zero
    {"impulse", OFF(RigidBody, impulse), FieldKind::DragV3},
};
/// KinematicBody | Jolt kinematic (script-driven) body: `colliders` = collider entities
static const FieldDesc kKinematicBody[] = {
    /// collider entities attached to this body; move it by writing its Transform
    {"colliders", OFF(KinematicBody, colliders), FieldKind::EntArr},
};
/// CapsuleShape | capsule collider shape: halfLen + radius
static const FieldDesc kCapsuleShape[] = {
    /// half the cylinder length (meters); total height = 2*halfLen + 2*radius
    {"halfLen", OFF(CapsuleShape, halfLen), FieldKind::SliderF, 0.0f, 10.0f},
    /// capsule radius (meters)
    {"radius", OFF(CapsuleShape, radius), FieldKind::SliderF, 0.0f, 5.0f},
};
/// BoxShape | box collider shape: `half` = {hx,hy,hz}, may also be written via hx..hz
static const FieldDesc kBoxShape[] = {
    /// half extents {hx, hy, hz} (meters)
    {"half", OFF(BoxShape, half), FieldKind::DragV3},
    /// slider alias for half.x
    {"hx", OFF(BoxShape, half), FieldKind::SliderF, 0.0f, 100.0f},
    /// slider alias for half.y
    {"hy", OFF(BoxShape, half) + 4, FieldKind::SliderF, 0.0f, 100.0f},
    /// slider alias for half.z
    {"hz", OFF(BoxShape, half) + 8, FieldKind::SliderF, 0.0f, 100.0f},
};
/// TaperedCapsule | tapered-capsule collider shape: halfHeight + bottom/top radius
static const FieldDesc kTaperedCapsule[] = {
    /// half the height (meters); 0 with equal radii is a sphere
    {"halfHeight", OFF(TaperedCapsule, halfHeight), FieldKind::SliderF, 0.0f, 10.0f},
    /// radius at the bottom end (meters)
    {"bottomRadius", OFF(TaperedCapsule, bottomRadius), FieldKind::SliderF, 0.0f, 5.0f},
    /// radius at the top end (meters)
    {"topRadius", OFF(TaperedCapsule, topRadius), FieldKind::SliderF, 0.0f, 5.0f},
};

/// SphereShape | sphere collider shape: radius
static const FieldDesc kSphereShape[] = {
    /// sphere radius (meters)
    {"radius", OFF(SphereShape, radius), FieldKind::SliderF, 0.0f, 10.0f},
};
/// ConvexHullShape | convex-hull collider shape: `points` = flat xyz triples of the hull vertices
static const FieldDesc kConvexHullShape[] = {
    /// flat {x,y,z,...} hull vertices (e.g. from physics.convex_decompose)
    {"points", OFF(ConvexHullShape, points), FieldKind::FloatArr},
};
/// Collider | links a shape entity to a body: `shape` = shape entity ref
static const FieldDesc kCollider[] = {
    /// the shape entity (CapsuleShape/BoxShape/TaperedCapsule/SphereShape/ConvexHullShape)
    {"shape", OFF(Collider, shape), FieldKind::Ref},
};
/// Joint | physics joint between two bodies (bodyA/bodyB refs); type (only 0 = point is supported); grabPos;
/// excludeCollision suppresses collision between the joined bodies
static const FieldDesc kJoint[] = {
    /// first body entity
    {"bodyA", OFF(Joint, bodyA), FieldKind::Ref},
    /// second body entity
    {"bodyB", OFF(Joint, bodyB), FieldKind::Ref},
    /// joint type; only 0 = point-to-point is supported
    {"type", OFF(Joint, type), FieldKind::SliderI, 0.0f, 1.0f},
    /// world-space point the two bodies are pinned together at
    {"grabPos", OFF(Joint, grabPos), FieldKind::DragV3},
    /// suppress collision between bodyA and bodyB
    {"excludeCollision", OFF(Joint, excludeCollision), FieldKind::Checkbox},
};
/// SixDofJoint | 6-DOF joint: bodyA/bodyB refs (null bodyB = fixed to world), translation/angle limits (lim*), soft
/// limits, per-axis motors (motor*), priority/solver steps, enabled
static const FieldDesc kSixDofJoint[] = {
    /// first body entity
    {"bodyA", OFF(SixDofJoint, bodyA), FieldKind::Ref},
    /// second body entity (null = fixed to the world)
    {"bodyB", OFF(SixDofJoint, bodyB), FieldKind::Ref},
    /// world-space point grabbed when the joint is created
    {"grabPos", OFF(SixDofJoint, grabPos), FieldKind::DragV3},
    /// interpret limits/motors in world space instead of body A's frame
    {"worldSpace", OFF(SixDofJoint, worldSpace), FieldKind::Checkbox},
    /// relative orientation locked between the two bodies {x,y,z,w}
    {"relQuat", OFF(SixDofJoint, relQuat), FieldKind::Quat},
    /// joint-axis frame rotation {x,y,z,w} (limits/motors are expressed in it)
    {"axisQuat", OFF(SixDofJoint, axisQuat), FieldKind::Quat},
    /// enable the translation/angle limits below
    {"limOn", OFF(SixDofJoint, limOn), FieldKind::Checkbox},
    /// min translation limit {x,y,z} (meters; use +/-1e30 to lock an axis)
    {"limTransMin", OFF(SixDofJoint, limTransMin), FieldKind::DragV3},
    /// max translation limit {x,y,z} (meters)
    {"limTransMax", OFF(SixDofJoint, limTransMax), FieldKind::DragV3},
    /// min swing/twist angle limits {x,y,z} (radians)
    {"limAngMin", OFF(SixDofJoint, limAngMin), FieldKind::DragV3},
    /// max swing/twist angle limits {x,y,z} (radians; 0 = locked)
    {"limAngMax", OFF(SixDofJoint, limAngMax), FieldKind::DragV3},
    /// soft-limit spring frequency per axis (Hz; 0 = hard limit)
    {"softTransFreq", OFF(SixDofJoint, softTransFreq), FieldKind::DragV3},
    /// soft-limit spring damping per axis
    {"softTransDamp", OFF(SixDofJoint, softTransDamp), FieldKind::DragV3},
    /// swing constraint shape: true = pyramid (box-like), false = cone
    {"pyramid", OFF(SixDofJoint, pyramid), FieldKind::Checkbox},
    /// idle friction per axis (translation)
    {"frictionTrans", OFF(SixDofJoint, frictionTrans), FieldKind::DragV3},
    /// idle friction per axis (rotation)
    {"frictionAng", OFF(SixDofJoint, frictionAng), FieldKind::DragV3},
    /// motor state per axis: 0 = off, 1 = velocity, 2 = position
    {"motorState", OFF(SixDofJoint, motorState), FieldKind::FloatArr},
    /// motor spring mode per axis: 0 = off, 1 = stiff, 2 = soft
    {"motorMode", OFF(SixDofJoint, motorMode), FieldKind::FloatArr},
    /// motor spring frequency per axis (Hz)
    {"motorFreq", OFF(SixDofJoint, motorFreq), FieldKind::DragV3},
    /// motor spring damping per axis
    {"motorDamp", OFF(SixDofJoint, motorDamp), FieldKind::DragV3},
    /// min motor force per axis (N)
    {"motorForceMin", OFF(SixDofJoint, motorForceMin), FieldKind::DragV3},
    /// max motor force per axis (N)
    {"motorForceMax", OFF(SixDofJoint, motorForceMax), FieldKind::DragV3},
    /// min motor torque per axis (N*m)
    {"motorTorqueMin", OFF(SixDofJoint, motorTorqueMin), FieldKind::DragV3},
    /// max motor torque per axis (N*m)
    {"motorTorqueMax", OFF(SixDofJoint, motorTorqueMax), FieldKind::DragV3},
    /// position-motor target offset {x,y,z} (meters)
    {"motorTargetPos", OFF(SixDofJoint, motorTargetPos), FieldKind::DragV3},
    /// velocity-motor target linear velocity {x,y,z} (m/s)
    {"motorTargetVel", OFF(SixDofJoint, motorTargetVel), FieldKind::DragV3},
    /// velocity-motor target angular velocity {x,y,z} (rad/s)
    {"motorTargetAngVel", OFF(SixDofJoint, motorTargetAngVel), FieldKind::DragV3},
    /// position-motor target orientation {x,y,z,w}
    {"motorTargetQuat", OFF(SixDofJoint, motorTargetQuat), FieldKind::Quat},
    /// solver priority (higher = solved first)
    {"priority", OFF(SixDofJoint, priority), FieldKind::SliderI, 0.0f, 1023.0f},
    /// velocity solver iterations for this joint
    {"velSteps", OFF(SixDofJoint, velSteps), FieldKind::SliderI, 0.0f, 64.0f},
    /// position solver iterations for this joint
    {"posSteps", OFF(SixDofJoint, posSteps), FieldKind::SliderI, 0.0f, 64.0f},
    /// enable/disable the joint without removing it
    {"enabled", OFF(SixDofJoint, enabled), FieldKind::Checkbox},
    /// suppress collision between bodyA and bodyB
    {"excludeCollision", OFF(SixDofJoint, excludeCollision), FieldKind::Checkbox},
};
/// ControllerInput | XR controller state: trigger_0/1, axis_0/1, button_0/1, active, aim pose (aim_pos + aim_qx..qw)
static const FieldDesc kControllerInput[] = {
    /// trigger pull 0..1 (read-only)
    {"trigger_0", OFF(ControllerInput, triggers), FieldKind::SliderF, 0.0f, 1.0f},
    /// squeeze pull 0..1 (read-only)
    {"trigger_1", OFF(ControllerInput, triggers) + 4, FieldKind::SliderF, 0.0f, 1.0f},
    /// thumbstick X -1..1 (read-only)
    {"axis_0", OFF(ControllerInput, axis), FieldKind::SliderF, -1.0f, 1.0f},
    /// thumbstick Y -1..1 (read-only)
    {"axis_1", OFF(ControllerInput, axis) + 4, FieldKind::SliderF, -1.0f, 1.0f},
    /// button A held (read-only)
    {"button_0", OFF(ControllerInput, buttons), FieldKind::Checkbox},
    /// button B held (read-only)
    {"button_1", OFF(ControllerInput, buttons) + 1, FieldKind::Checkbox},
    /// true while this controller is tracked (read-only)
    {"active", OFF(ControllerInput, active), FieldKind::Checkbox},
    /// world-space aim point of the controller (read-only)
    {"aim_pos", OFF(ControllerInput, aimPos), FieldKind::DragV3},
    /// slider alias for the aim quaternion x (read-only)
    {"aim_qx", OFF(ControllerInput, aimQuat), FieldKind::SliderF, -1.0f, 1.0f},
    /// slider alias for the aim quaternion y (read-only)
    {"aim_qy", OFF(ControllerInput, aimQuat) + 4, FieldKind::SliderF, -1.0f, 1.0f},
    /// slider alias for the aim quaternion z (read-only)
    {"aim_qz", OFF(ControllerInput, aimQuat) + 8, FieldKind::SliderF, -1.0f, 1.0f},
    /// slider alias for the aim quaternion w (read-only)
    {"aim_qw", OFF(ControllerInput, aimQuat) + 12, FieldKind::SliderF, -1.0f, 1.0f},
};
/// MouseInput | mouse state: x/y position, button_0..3
static const FieldDesc kMouseInput[] = {
    /// cursor X in window pixels (read-only)
    {"x", OFF(MouseInput, x), FieldKind::SliderF, 0.0f, 3840.0f},
    /// cursor Y in window pixels (read-only)
    {"y", OFF(MouseInput, y), FieldKind::SliderF, 0.0f, 2160.0f},
    /// left mouse button held (read-only; frozen while ImGui has the mouse)
    {"button_0", OFF(MouseInput, buttons), FieldKind::Checkbox},
    /// right mouse button held (read-only)
    {"button_1", OFF(MouseInput, buttons) + 1, FieldKind::Checkbox},
    /// middle mouse button held (read-only)
    {"button_2", OFF(MouseInput, buttons) + 2, FieldKind::Checkbox},
    /// fourth mouse button held (read-only)
    {"button_3", OFF(MouseInput, buttons) + 3, FieldKind::Checkbox},
};
/// Renderable | makes an entity renderable: `geom` = geometry entity ref, noShadow, visible
static const FieldDesc kRenderable[] = {
    /// the Geometry entity to draw (defaults to the node's own Geometry in scene.spawn)
    {"geom", 0, FieldKind::Ref},
    /// exclude this mesh from shadow maps
    {"noShadow", OFF(Renderable, noShadow), FieldKind::Checkbox},
    /// false hides the mesh without removing the component
    {"visible", OFF(Renderable, visible), FieldKind::Checkbox},
};
/// Texture | texture from a file: `path`, `space` = "linear" or "srgb" (sRGB data is decoded on upload)
static const FieldDesc kTexture[] = {
    /// image file path (resolved against the assets dir; loaded lazily on first use)
    {"path", OFF(Texture, path), FieldKind::Str},
    /// color space: "linear" or "srgb" (sRGB data is decoded on upload)
    {"space", OFF(Texture, space), FieldKind::Str},
};
/// Geometry | mesh data: pos/nrm/uv float arrays, idx/joints int arrays, weights float array (per-vertex skin weights)
static const FieldDesc kGeometry[] = {
    /// vertex positions, flat xyz-interleaved
    {"pos", OFF(Geometry, mesh.pos), FieldKind::FloatArr},
    /// vertex normals, flat xyz-interleaved (same count as pos)
    {"nrm", OFF(Geometry, mesh.nrm), FieldKind::FloatArr},
    /// UVs, flat uv pairs (same count as pos)
    {"uv", OFF(Geometry, mesh.uv), FieldKind::FloatArr},
    /// triangle indices, 0-based (3 per triangle)
    {"idx", OFF(Geometry, mesh.indices), FieldKind::UIntArr},
    /// skinning: 4 bone indices per vertex (boneNames order of the Skeleton)
    {"joints", OFF(Geometry, mesh.joints), FieldKind::UIntArr},
    /// skinning: 4 weights per vertex (normalized; may sum to <1)
    {"weights", OFF(Geometry, mesh.weights), FieldKind::FloatArr},
};
/// Skeleton | skinning skeleton: name, boneNames string array, joints entity array, placement ref, meshes entity array;
/// invBinds is derived on write
static const FieldDesc kSkeleton[] = {
    /// display/lookup name of the skeleton
    {"name", OFF(Skeleton, name), FieldKind::Str},
    /// bone names in joint order (Geometry.joints index into this)
    {"boneNames", OFF(Skeleton, boneNames), FieldKind::StrArr},
    /// bone entities (Transforms), one per boneName; posed from their world transforms
    {"joints", OFF(Skeleton, joints), FieldKind::EntArr},
    /// scene node the skeleton is anchored to (AnimationPlayers parent under it)
    {"placement", OFF(Skeleton, placement), FieldKind::Ref},
    /// skinned mesh entities posed by this skeleton (also get scene.set_morph weights)
    {"meshes", OFF(Skeleton, meshes), FieldKind::EntArr},
};
/// RenderSettings | global render settings (emplace on a marker entity): bgColor (vec3), exposure, sssDepthReject,
/// sssCurvScale, renderScale (scene buffer size vs display; >1 supersamples)
static const FieldDesc kRenderSettings[] = {
    /// clear color when no environment is set
    {"bgColor", OFF(RenderSettings, bgColor), FieldKind::Color},
    /// tone-mapping exposure multiplier
    {"exposure", OFF(RenderSettings, exposure), FieldKind::SliderF, 0.1f, 3.0f},
    /// SSS pass: reject samples whose depth differs by more than this (meters)
    {"sssDepthReject", OFF(RenderSettings, sssDepthReject), FieldKind::SliderF, 0.0f, 8.0f},
    /// SSS pass: curvature scale for the scatter kernel
    {"sssCurvScale", OFF(RenderSettings, sssCurvScale), FieldKind::SliderF, 0.0f, 10.0f},
    /// scene buffer size vs display (1 = native; >1 supersamples)
    {"renderScale", OFF(RenderSettings, renderScale), FieldKind::SliderF, 0.25f, 2.0f},
};
/// PhysicsSettings | global physics solver steps (emplace on a marker entity): velocitySteps/positionSteps
static const FieldDesc kPhysicsSettings[] = {
    /// Jolt velocity solver iterations per step
    {"velocitySteps", OFF(PhysicsSettings, velocitySteps), FieldKind::SliderI, 1.0f, 512.0f},
    /// Jolt position solver iterations per step
    {"positionSteps", OFF(PhysicsSettings, positionSteps), FieldKind::SliderI, 1.0f, 512.0f},
    /// fixed physics tick rate in Hz (accumulator steps the sim at this rate regardless of display rate)
    {"physHz", OFF(PhysicsSettings, physHz), FieldKind::SliderI, 1.0f, 1000.0f},
};

/// Bind | per-frame URL copy: source `url` -> `target` field (empty = widget value); `dir` 0=Both 1=Read 2=Write
static const FieldDesc kBind[] = {
    /// source URL ("Name.Component.field" or "name.LuaScript.var")
    {"url", OFF(Bind, url), FieldKind::Str},
    /// destination field of this entity (empty = the widget's own value)
    {"target", OFF(Bind, target), FieldKind::Str},
    /// 0 = Both, 1 = Read (display only), 2 = Write (write-back only)
    {"dir", OFF(Bind, dir), FieldKind::SliderI, 0.0f, 2.0f},
};
/// AnimationClip | keyframe clip asset: duration + per-track target URLs and flat keyframe arrays
/// (times/values concatenated; tangents for cubic). glTF clips are populated eagerly by loadGltf/gltfToData.
static const FieldDesc kAnimationClip[] = {
    /// clip length in seconds (auto-derived from the last keyframe when 0)
    {"duration", OFF(AnimationClip, duration), FieldKind::SliderF, 0.0f, 3600.0f},
    /// one target URL per track ("Name.Component.field")
    {"targets", OFF(AnimationClip, targets), FieldKind::StrArr},
    /// per-track interpolation: 0 = linear, 1 = step, 2 = cubic
    {"interps", OFF(AnimationClip, interps), FieldKind::FloatArr},
    /// track i owns keyframes [trackStart[i], trackStart[i+1]) (n+1 entries, last = total)
    {"trackStart", OFF(AnimationClip, trackStart), FieldKind::FloatArr},
    /// keyframe times, concatenated per track (sorted within a track)
    {"times", OFF(AnimationClip, times), FieldKind::FloatArr},
    /// keyframe values, concatenated per track (field width x key count per track)
    {"values", OFF(AnimationClip, values), FieldKind::FloatArr},
    /// optional cubic tangents: 2*(n-1)*width floats per track, [outT0, inT1, outT1, ...]
    {"tangents", OFF(AnimationClip, tangents), FieldKind::FloatArr},
};
/// AnimationPlayer | plays an AnimationClip: `clip` = clip entity ref, time/speed, loop 0=once 1=loop 2=pingpong,
/// playing start/stop
static const FieldDesc kAnimationPlayer[] = {
    /// the AnimationClip entity to play
    {"clip", OFF(AnimationPlayer, clip), FieldKind::Ref},
    /// playback time in seconds (advanced by dt*speed while playing)
    {"time", OFF(AnimationPlayer, time), FieldKind::SliderF, 0.0f, 3600.0f},
    /// playback rate (negative = reverse)
    {"speed", OFF(AnimationPlayer, speed), FieldKind::SliderF, -4.0f, 4.0f},
    /// 0 = once (clamped), 1 = loop, 2 = pingpong
    {"loop", OFF(AnimationPlayer, loop), FieldKind::SliderI, 0.0f, 2.0f},
    /// pause/resume playback
    {"playing", OFF(AnimationPlayer, playing), FieldKind::Checkbox},
};

/// UIWidget | marker for a drawable UI widget; `order` is its layout position among siblings
static const FieldDesc kUIWidget[] = {
    /// layout position among siblings (call ui.seq() when creating by hand)
    {"order", OFF(UIWidget, order), FieldKind::SliderI, 0.0f, 10000.0f},
};
/// UIButton | a clickable button; on click the exported function named by `target_url` is called with this entity as
/// arg 1
static const FieldDesc kUIButton[] = {
    /// button text
    {"label", OFF(UIButton, label), FieldKind::Str},
    /// "name.LuaScript.fn" called on click (this entity as arg 1)
    {"target_url", OFF(UIButton, target_url), FieldKind::Str},
};
/// UICanvas | a UI canvas: its own GPU texture + quad; `w`/`h` are the full size in meters
static const FieldDesc kUICanvas[] = {
    /// canvas width in world meters (pixels * 0.0022)
    {"w", OFF(UICanvas, worldSize), FieldKind::SliderF},
    /// canvas height in world meters
    {"h", OFF(UICanvas, worldSize) + 4, FieldKind::SliderF},
};
/// GPUTexture | an allocated GPU texture (pixel size); created for canvas entities
static const FieldDesc kGPUTexture[] = {
    /// width in pixels
    {"w", OFF(GPUTexture, w), FieldKind::SliderI, 0.0f, 16384.0f},
    /// height in pixels
    {"h", OFF(GPUTexture, h), FieldKind::SliderI, 0.0f, 16384.0f},
};
/// UIWindow | an ImGui window filling its canvas; `pos` comes from the canvas Transform (set it explicitly)
static const FieldDesc kUIWindow[] = {
    /// window title bar text
    {"title", OFF(UIWindow, title), FieldKind::Str},
    /// the UICanvas entity this window is drawn on
    {"canvas", OFF(UIWindow, canvas), FieldKind::Ref},
    /// window width in pixels (the canvas Transform places it)
    {"w", OFF(UIWindow, size), FieldKind::SliderF},
    /// window height in pixels
    {"h", OFF(UIWindow, size) + 4, FieldKind::SliderF},
    /// ImGui window flags (e.g. 294 = no move/collapse/resize/settings)
    {"flags", OFF(UIWindow, flags), FieldKind::SliderI, 0.0f, 1000000.0f},
    /// layout order among windows on the same canvas
    {"order", OFF(UIWindow, order), FieldKind::SliderI, 0.0f, 10000.0f},
};
/// UITab | a tab page inside a UI window
static const FieldDesc kUITab[] = {
    /// tab label; widgets parented under it draw on this page
    {"label", OFF(UITab, label), FieldKind::Str},
};
/// UIText | a static (or bound) text line
static const FieldDesc kUIText[] = {
    /// the text to show (live-updates when bound via Bind)
    {"label", OFF(UIText, label), FieldKind::Str},
};
/// UISection | a (collapsible by default) labeled section; its children are added to it
static const FieldDesc kUISection[] = {
    /// section header text
    {"label", OFF(UISection, label), FieldKind::Str},
    /// collapse/expand the section
    {"collapsible", OFF(UISection, collapsible), FieldKind::Checkbox},
};
/// UISliderF | a float slider; `change_url` is called with this entity as arg 1 while the user drags it
static const FieldDesc kUISliderF[] = {
    /// slider label
    {"label", OFF(UISliderF, label), FieldKind::Str},
    /// current value (bound live via Bind when present)
    {"value", OFF(UISliderF, value), FieldKind::SliderF},
    /// minimum
    {"lo", OFF(UISliderF, lo), FieldKind::SliderF},
    /// maximum
    {"hi", OFF(UISliderF, hi), FieldKind::SliderF},
    /// printf format for the readout (e.g. "%.2f")
    {"fmt", OFF(UISliderF, fmt), FieldKind::Str},
    /// "name.LuaScript.fn" called while the user drags (this entity as arg 1)
    {"change_url", OFF(UISliderF, change_url), FieldKind::Str},
};
/// UISliderI | an integer slider; `change_url` is called with this entity as arg 1 while the user drags it
static const FieldDesc kUISliderI[] = {
    /// slider label
    {"label", OFF(UISliderI, label), FieldKind::Str},
    /// current value (bound live via Bind when present)
    {"value", OFF(UISliderI, value), FieldKind::SliderI},
    /// minimum
    {"lo", OFF(UISliderI, lo), FieldKind::SliderI},
    /// maximum
    {"hi", OFF(UISliderI, hi), FieldKind::SliderI},
    /// "name.LuaScript.fn" called while the user drags (this entity as arg 1)
    {"change_url", OFF(UISliderI, change_url), FieldKind::Str},
};
/// UICheckbox | a bool checkbox; `change_url` is called with this entity as arg 1 when toggled
static const FieldDesc kUICheckbox[] = {
    /// checkbox label
    {"label", OFF(UICheckbox, label), FieldKind::Str},
    /// checked state (bound live via Bind when present)
    {"value", OFF(UICheckbox, value), FieldKind::Checkbox},
    /// "name.LuaScript.fn" called when toggled (this entity as arg 1)
    {"change_url", OFF(UICheckbox, change_url), FieldKind::Str},
};
/// UIColorEdit | an RGB (0..1) color picker; `change_url` is called with this entity as arg 1 while the user edits it
static const FieldDesc kUIColorEdit[] = {
    /// picker label
    {"label", OFF(UIColorEdit, label), FieldKind::Str},
    /// current color {r,g,b} (bound live via Bind when present)
    {"value", OFF(UIColorEdit, value), FieldKind::Color},
    /// "name.LuaScript.fn" called while the user edits (this entity as arg 1)
    {"change_url", OFF(UIColorEdit, change_url), FieldKind::Str},
};
/// UIDragV3 | a 3D drag gizmo; `change_url` is called with this entity as arg 1 while the user drags it
static const FieldDesc kUIDragV3[] = {
    /// gizmo label
    {"label", OFF(UIDragV3, label), FieldKind::Str},
    /// current value {x,y,z} (bound live via Bind when present)
    {"value", OFF(UIDragV3, value), FieldKind::DragV3},
    /// drag sensitivity (units per pixel)
    {"speed", OFF(UIDragV3, speed), FieldKind::SliderF},
    /// "name.LuaScript.fn" called while the user drags (this entity as arg 1)
    {"change_url", OFF(UIDragV3, change_url), FieldKind::Str},
};

static const TypeDesc kTypes[] = {
    {"Transform", kTransform},
    {"Bind", kBind},
    {"AnimationClip", kAnimationClip},
    {"AnimationPlayer", kAnimationPlayer},
    {"UIWidget", kUIWidget},
    {"UIButton", kUIButton},
    {"UICanvas", kUICanvas},
    {"GPUTexture", kGPUTexture},
    {"UIWindow", kUIWindow},
    {"UITab", kUITab},
    {"UIText", kUIText},
    {"UISection", kUISection},
    {"UISliderF", kUISliderF},
    {"UISliderI", kUISliderI},
    {"UICheckbox", kUICheckbox},
    {"UIColorEdit", kUIColorEdit},
    {"UIDragV3", kUIDragV3},
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

// when new entities join, e.g. a glb load) and components can be re-emplaced, so
// callers resolve on every draw instead of capturing once
template <typename T> std::function<const void*()> liveRef(const entt::registry& reg, entt::entity e) {
    return [&reg, e] -> const void* {
        if (auto* p = reg.try_get<T>(e))
            return (const void*)p;
        return nullptr;
    };
}

inline std::vector<std::pair<const TypeDesc*, std::function<const void*()>>> entityFields(const entt::registry& reg,
                                                                                          entt::entity e) {
    std::vector<std::pair<const TypeDesc*, std::function<const void*()>>> out;
    auto add = [&out](const char* tn, std::function<const void*()> fn) {
        out.emplace_back(findType(tn), std::move(fn));
    };
    if (reg.try_get<Transform>(e))
        add("Transform", liveRef<Transform>(reg, e));
    if (reg.try_get<Material>(e))
        add("Material", liveRef<Material>(reg, e));
    if (reg.try_get<MaterialRef>(e))
        add("MaterialRef", liveRef<MaterialRef>(reg, e));
    if (reg.try_get<Parent>(e))
        add("Parent", liveRef<Parent>(reg, e));
    if (reg.try_get<Camera>(e))
        add("Camera", liveRef<Camera>(reg, e));
    if (reg.try_get<Environment>(e))
        add("Environment", liveRef<Environment>(reg, e));
    if (reg.try_get<PointLight>(e))
        add("PointLight", liveRef<PointLight>(reg, e));
    if (reg.try_get<SoftBodyData>(e))
        add("SoftBodyData", liveRef<SoftBodyData>(reg, e));
    if (reg.try_get<RigidBody>(e))
        add("RigidBody", liveRef<RigidBody>(reg, e));
    if (reg.try_get<KinematicBody>(e))
        add("KinematicBody", liveRef<KinematicBody>(reg, e));
    if (reg.try_get<CapsuleShape>(e))
        add("CapsuleShape", liveRef<CapsuleShape>(reg, e));
    if (reg.try_get<BoxShape>(e))
        add("BoxShape", liveRef<BoxShape>(reg, e));
    if (reg.try_get<TaperedCapsule>(e))
        add("TaperedCapsule", liveRef<TaperedCapsule>(reg, e));
    if (reg.try_get<SphereShape>(e))
        add("SphereShape", liveRef<SphereShape>(reg, e));
    if (reg.try_get<ConvexHullShape>(e))
        add("ConvexHullShape", liveRef<ConvexHullShape>(reg, e));
    if (reg.try_get<Collider>(e))
        add("Collider", liveRef<Collider>(reg, e));
    if (reg.try_get<Joint>(e))
        add("Joint", liveRef<Joint>(reg, e));
    if (reg.try_get<SixDofJoint>(e))
        add("SixDofJoint", liveRef<SixDofJoint>(reg, e));
    if (reg.try_get<ControllerInput>(e))
        add("ControllerInput", liveRef<ControllerInput>(reg, e));
    if (reg.try_get<Renderable>(e))
        add("Renderable", liveRef<Renderable>(reg, e));
    if (reg.try_get<Texture>(e))
        add("Texture", liveRef<Texture>(reg, e));
    if (reg.try_get<Geometry>(e))
        add("Geometry", liveRef<Geometry>(reg, e));
    if (reg.try_get<Skeleton>(e))
        add("Skeleton", liveRef<Skeleton>(reg, e));
    if (reg.try_get<MouseInput>(e))
        add("MouseInput", liveRef<MouseInput>(reg, e));
    if (reg.try_get<RenderSettings>(e))
        add("RenderSettings", liveRef<RenderSettings>(reg, e));
    if (reg.try_get<PhysicsSettings>(e))
        add("PhysicsSettings", liveRef<PhysicsSettings>(reg, e));
    if (reg.try_get<Bind>(e))
        add("Bind", liveRef<Bind>(reg, e));
    if (reg.try_get<AnimationClip>(e))
        add("AnimationClip", liveRef<AnimationClip>(reg, e));
    if (reg.try_get<AnimationPlayer>(e))
        add("AnimationPlayer", liveRef<AnimationPlayer>(reg, e));
    return out;
}
} // namespace refl
