#pragma once
#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

#include "math.hpp"
#include "sim/params.hpp"
#include <entt/entt.hpp>
#include <functional>
#include <string>

struct Transform {
    V3 pos{0, 0, 0};
    V4 quat{0, 0, 0, 1};

    Mat4 toMat4() const {
        const float q[4] = {quat.x, quat.y, quat.z, quat.w};
        float rot[9];
        quatToMat3(q, rot);
        Mat4 m{};
        m.m[0] = rot[0];
        m.m[1] = rot[1];
        m.m[2] = rot[2];
        m.m[4] = rot[3];
        m.m[5] = rot[4];
        m.m[6] = rot[5];
        m.m[8] = rot[6];
        m.m[9] = rot[7];
        m.m[10] = rot[8];
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

inline Transform transformCompose(const Transform& parent, const Transform& local) {
    Transform r;
    r.quat = quatMul(parent.quat, local.quat);
    r.pos = vAdd(parent.pos, quatRotate(parent.quat, local.pos));
    return r;
}

// inverse of a rigid (rotation + translation) pose
inline Transform transformInvert(const Transform& t) {
    return {vScale(quatRotate(quatInv(t.quat), t.pos), -1.0f), quatInv(t.quat)};
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

struct RigidBodyProps {
    float friction = 0.7f;
    float restitution = 0.05f;
    float damping = 0.05f;
};

struct CapsuleCollider {
    int slot = -1;
};

struct RigidDynamics {
    RigidBodyProps props;
    JPH::BodyID id;
};

struct SoftBodyData {
    int softId = -1;
    int pinned = 0;
    SimParams params;
    int steps = kDefaultSteps;
};

// camera: pose lives in the entity's Transform, projection params here; active = the current
// camera the window renders (exactly one by convention). The engine swaps it per display mode:
// the VR head camera ("vr cam") while presenting, the scene's fly camera otherwise.
struct Camera {
    float fovDeg = 50.0f;
    float nearP = 0.1f;
    float farP = 300.0f;
    bool active = false;
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

// a kinematic Jolt body driven by the engine (controller hand anchor for joints)
struct KinematicBody {
    JPH::BodyID id{};
};

// mouse pointer state (window-relative); "mouse" entity
struct MouseInput {
    float x = 0.0f;
    float y = 0.0f;
    bool buttons[4] = {}; // L, R, M, back
};

// keyboard state (GLFW key codes); "keyboard" entity
struct KeyboardInput {
    bool keys[352] = {};
};

struct Renderable {
    int inst = -1;
};

// owned by material entities only; meshes point at it via MaterialRef, so many meshes can share
// one material (and editing it updates all users)
struct Material {
    V3 baseColor{1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    V3 emissive{0.0f, 0.0f, 0.0f}; // added to the lit color before tonemap (HDR values glow)
    int texId = -1;                // renderer texture (albedo); -1 = none
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
