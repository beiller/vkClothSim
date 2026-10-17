#pragma once
#include "math.hpp"
#include "mesh.hpp"
#include "sim/params.hpp"
#include "xr_frame.hpp"
#include <entt/entt.hpp>
#include <functional>
#include <string>
#include <vector>

struct Transform {
    V3 pos{0, 0, 0};
    V4 quat{0, 0, 0, 1};
    V3 scale{1, 1, 1};

    Mat4 toMat4() const { // T * R * S (scale in the local frame)
        const float q[4] = {quat.x, quat.y, quat.z, quat.w};
        float rot[9];
        quatToMat3(q, rot);
        Mat4 m{};
        m.m[0] = rot[0] * scale.x;
        m.m[1] = rot[1] * scale.x;
        m.m[2] = rot[2] * scale.x;
        m.m[4] = rot[3] * scale.y;
        m.m[5] = rot[4] * scale.y;
        m.m[6] = rot[5] * scale.y;
        m.m[8] = rot[6] * scale.z;
        m.m[9] = rot[7] * scale.z;
        m.m[10] = rot[8] * scale.z;
        m.m[12] = pos.x;
        m.m[13] = pos.y;
        m.m[14] = pos.z;
        m.m[15] = 1.0f;
        return m;
    }
};

// resolved world-space pose; same Transform class, written by the resolve system
struct WorldTransform : Transform {};

// hierarchy link: this entity is a child of e
struct Parent {
    entt::entity e;
};

// parent * local (TRS); a non-uniform parent scale combined with a local rotation is
// approximated componentwise (the usual DCC-tool caveat)
inline Transform transformCompose(const Transform& parent, const Transform& local) {
    Transform r;
    r.quat = quatMul(parent.quat, local.quat);
    r.scale = {parent.scale.x * local.scale.x, parent.scale.y * local.scale.y, parent.scale.z * local.scale.z};
    r.pos = vAdd(parent.pos,
                 quatRotate(parent.quat,
                            V3{local.pos.x * parent.scale.x, local.pos.y * parent.scale.y, local.pos.z * parent.scale.z}));
    return r;
}

// inverse of a scaled pose; exact for uniform scale, componentwise otherwise
inline Transform transformInvert(const Transform& t) {
    const V3 r = quatRotate(quatInv(t.quat), t.pos);
    return {{-r.x / t.scale.x, -r.y / t.scale.y, -r.z / t.scale.z},
            quatInv(t.quat),
            {1.0f / t.scale.x, 1.0f / t.scale.y, 1.0f / t.scale.z}};
}

// per-entity Lua behavior script: file is a module (loaded once, cached) with optional
// lifecycle hooks _on_ready(self) / _animate(self, dt) / _physics(self, dt). The inst/hook
// registry refs are owned by SceneScript (unrefed on destroy/shutdown).
struct LuaScript {
    std::string file; // include-relative module path
    int inst = -1;    // registry ref to the per-entity instance table (self)
    int params = -1;  // registry ref to a read-only per-entity config table (self.params)
    int onReady = -1; // ref to _on_ready  (-1 = absent)
    int animate = -1; // ref to _animate   (-1)
    int physics = -1; // ref to _physics   (-1)
    bool ready = false;
};

// shared collision geometry: one shape entity referenced by many bodies' colliders. Pure data:
// the rigid sim holds its own lazy Jolt projection (built on first use), the soft sim reads the
// geometry live each step
struct CapsuleShape {
    float halfLen = 0.0f;
    float radius = 0.0f;
};

struct BoxShape {
    V3 half{};
};

// a body's collision: a shared shape entity + a local Transform placing it in the body's frame.
// Collider entities are children of a RigidBody or KinematicBody entity; the CPU sim builds the
// Jolt body from them at spawn, the GPU soft sim reads the capsule ones (boxes are CPU-sim only)
struct Collider {
    entt::entity shape = entt::null;
};

// a Jolt rigid body integrated by the CPU sim: its pose is written back to the entity's Transform
// each step (the collider children's world poses then feed the GPU soft sim). The material
// params apply at spawn; the Jolt body is not updated after creation. The body is built lazily
// from the entity's collider children on the first sim step after this component appears
struct RigidBody {
    float friction = 0.7f;
    float restitution = 0.05f;
    float damping = 0.05f;
    bool isStatic = false; // static = world-anchored (no integration)
};

// a kinematic body constraint: a fixed-space point between bodyA and bodyB (a point constraint);
// bodyB == null anchors bodyA to the world. type: 0 = point. The constraint is created lazily
// once both endpoint bodies exist. A Joint entity is a standalone (usually parented under one
// endpoint so it cascades on destroy); destroying it releases the constraint
struct Joint {
    entt::entity bodyA = entt::null;
    entt::entity bodyB = entt::null; // entt::null = world anchor
    int type = 0;
    V3 grabPos{}; // world-space point the constraint is created through
};

struct SoftBodyData {
    int pinned = 0;
    SimParams params;
    int steps = kDefaultSteps;
    // flat {a, b, rest, k} quads, 0-based vertex indices; the soft sim reads the vertex data
    // from the Renderable's geometry and builds the body on its first sim step
    std::vector<float> cons;
};

// camera: pose lives in the entity's Transform, projection params here; active = the current
// camera the window renders (exactly one by convention). The engine swaps it per display mode:
// the VR head camera ("vr cam") while presenting, the scene's fly camera otherwise.
// a camera is a set of views, each of which renders one image. A single camera has one view,
// derived from the entity's Transform + fov (nViews = 0). A stereo head (the VR camera) stores
// its two eye views here: per-eye pose + lens tangents, filled each frame from the headset poll.
struct Camera {
    float fovDeg = 50.0f;
    float nearP = 0.1f;
    float farP = 300.0f;
    bool active = false;
    int nViews = 0; // 0 = single view (the entity's Transform), 2 = stereo eyes
    View views[2];
};

// a tracked controller (left/right). Its Transform is the grip pose, local under the VR root; the
// aim pose (ref space) + inputs feed grab/locomotion from Lua. Resolved to world by the rig chain.
struct ControllerInput {
    int hand = -1;          // 0 left, 1 right
    float triggers[2] = {}; // 0 = trigger, 1 = squeeze (0..1)
    float axis[2] = {};     // stick x, y (-1..1)
    bool buttons[2] = {};   // 0 = A, 1 = B (pressed)
    V3 aimPos{};            // ref space
    V4 aimQuat{0, 0, 0, 1};
    bool active = false;
};

// marker: the entity owns a kinematic Jolt body (grab anchor etc.) driven from its Transform;
// the rigid sim holds the entity -> body mapping
struct KinematicBody {};

// mouse pointer state (window-relative); "mouse" entity
struct MouseInput {
    float x = 0.0f;
    float y = 0.0f;
    bool buttons[4] = {}; // L, R, M, back
};

// keyboard state (GLFW key codes); "keyboard" entity
struct KeyboardInput {
    static constexpr int kMaxKeys = 352; // past GLFW_KEY_LAST (349)
    bool keys[kMaxKeys] = {};
};

// draw visibility is per-mesh: an entity is drawn when visible (no ancestor walk). The geom
// entity holds the vertex data (a Geometry component); many renderables may share one
struct Renderable {
    entt::entity geom = entt::null; // the geometry entity (shared vertex data)
    bool noShadow = false; // skip in the shadow pass (still drawn in the scene pass)
    bool visible = true; // false = not drawn in any pass

    Renderable() = default;
    Renderable(entt::entity g, bool ns = false) : geom(g), noShadow(ns) {}
};

// vertex data, owned by a geometry entity; renderables reference it. The renderer uploads it
// to the GPU on first sight (shared across all referencing renderables)
struct Geometry {
    Mesh mesh;
};

// one per skinned character (a glTF skin): the ECS source of truth for a skeleton. The Skin
// manager owns the GPU resources and discovers skeletons by viewing this component in step(), so
// any number of skeletons (characters) are supported. joints are in the skin's joints[] order;
// placement is the representative node whose WorldTransform carries the whole character rigidly
// (excluded from the per-joint deformation matrix). meshes[] is the forward ownership list of
// this skeleton's skinned prim entities (they alias the skeleton's group vertex buffers); the
// skeleton is a child of the glTF root entity so destroying the root cascades to it (freezing
// the meshes in their last pose)
struct Skeleton {
    std::string name;
    std::vector<entt::entity> joints;
    std::vector<Mat4> invBinds;
    entt::entity placement = entt::null;
    std::vector<entt::entity> meshes;
};

// shared texture entity: one per asset path; materials link by entity ref, so users share one
// GPU copy. The renderer uploads the file lazily on first sight (a file-backed path); glTF
// embedded textures are uploaded eagerly at import (their bytes exist only then)
struct Texture {
    std::string path;
};

// owned by material entities only; meshes point at it via MaterialRef, so many meshes can share
// one material (and editing it updates all users). The map slots mirror glTF PBR textures;
// factors multiply the sampled texel (a null slot falls back to a neutral 1x1 in the shader)
struct Material {
    V3 baseColor{1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    V3 emissive{0.0f, 0.0f, 0.0f}; // added to the lit color before tonemap (HDR values glow)
    entt::entity tex = entt::null;        // base color map (albedo)
    entt::entity texMR = entt::null;      // metallicRoughness map (glTF channels: G = rough, B = metal)
    entt::entity texNormal = entt::null;  // tangent-space normal map
    entt::entity texEmissive = entt::null; // emissive map (multiplied by the emissive factor)
    // glTF alpha: mode 0 = opaque, 1 = mask (alpha test), 2 = blend; alpha multiplies the map
    int alphaMode = 0;
    float alphaCutoff = 0.5f;
    float alpha = 1.0f;
};

// mesh -> shared material entity
struct MaterialRef {
    entt::entity mat;
};

struct Name {
    std::string id;
};

// ---- UI: windows and widgets are entities. A widget is a child (Parent) of its window or
// section, drawn inside it by drawUi in spawn order. UI entities carry no Transform.

struct UIWindow {
    std::string title;
    std::function<V2()> pos; // fn so e.g. the fps HUD can track the window size
    V2 size{};               // zero = no forced size
    int flags = 0;
    bool collapseOnAppear = false;
    int order = 0;
};

// marker on every widget entity; order drives draw order
struct UIWidget {
    int order = 0;
};

// value bindings yield a pointer to the edited state; the lambda lives in the widget

struct UISliderF {
    std::string label;
    float lo, hi;
    std::string fmt = "%.2f";
    std::function<float*()> value;
};

struct UISliderI {
    std::string label;
    int lo, hi;
    std::function<int*()> value;
};

struct UICheckbox {
    std::string label;
    std::function<bool*()> value;
};

struct UIColorEdit {
    std::string label;
    std::function<float*()> value;
};

struct UIDragV3 {
    std::string label;
    float speed = 0.1f;
    std::function<float*()> value;
};

struct UIText {
    std::string label; // static text; per-frame text() overrides when set
    std::function<std::string()> text;
};

struct UIButton {
    std::string label;
    std::function<void()> on;
};

struct UISection {
    std::string label;
    bool collapsible = false; // false = SeparatorText, true = CollapsingHeader; children are the body
};

// tab page; direct children of a window form the tab bar (convention), children are the page body
struct UITab {
    std::string label;
};
