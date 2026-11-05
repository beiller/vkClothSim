#pragma once
#include "mesh.hpp"
#include "math.hpp"
#include "params.hpp"
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>
#include <vector>

struct Transform {
    V3 pos{0, 0, 0};
    Quat quat{1, 0, 0, 0};
    V3 scale{1, 1, 1};

    Mat4 toMat4() const {
        Mat4 m = mat4_cast(quat);
        m[0] *= scale.x;
        m[1] *= scale.y;
        m[2] *= scale.z;
        m[3] = {pos.x, pos.y, pos.z, 1.0f};
        return m;
    }
};

struct WorldTransform : Transform {};

struct Parent {
    entt::entity e;
};

inline Transform transformCompose(const Transform& parent, const Transform& local) {
    Transform r;
    r.quat = parent.quat * local.quat;
    r.scale = parent.scale * local.scale;
    r.pos = parent.pos + parent.quat * (local.pos * parent.scale);
    return r;
}

inline Transform transformInvert(const Transform& t) {
    const Quat qi = glm::inverse(t.quat);
    const V3 r = qi * t.pos;
    return {{-r.x / t.scale.x, -r.y / t.scale.y, -r.z / t.scale.z},
            qi,
            {1.0f / t.scale.x, 1.0f / t.scale.y, 1.0f / t.scale.z}};
}

// marker: the VR head camera (child of the VR root)
struct VrCamera {};

// marker: the VR headset root (walk origin)
struct VrRoot {};

struct LuaScript {
    std::string file;
    int inst = -1;
    int params = -1;
    int onReady = -1;
    int animate = -1;
    int physics = -1;
    bool ready = false;
    // C++ mirror of the script's `inst.vars` table: URL bind targets ("name.LuaScript.var").
    // Numbers are stored as float (dotted keys for nested tables); 3/4-number tables get a
    // contiguous vec slot (subvars like "pos.x" resolve to member views into it). The
    // script host syncs these with the lua table each frame; pulls never erase entries,
    // keeping bound UrlRef slots stable.
    std::unordered_map<std::string, float> varsF;
    std::unordered_map<std::string, bool> varsB;
    std::unordered_map<std::string, std::string> varsS;
    std::unordered_map<std::string, V3> varsV3;
    std::unordered_map<std::string, Quat> varsQ;
};

struct CapsuleShape {
    float halfLen = 0.0f;
    float radius = 0.0f;
};

struct BoxShape {
    V3 half{};
};

struct TaperedCapsule {
    // Jolt layout: centered at the origin, axis = local Y; the radius ramps linearly
    // from bottomRadius (at -Y * halfHeight) to topRadius (at +Y * halfHeight)
    float halfHeight = 0.0f;
    float bottomRadius = 0.0f;
    float topRadius = 0.0f;
};

struct SphereShape {
    float radius = 0.0f;
};

struct ConvexHullShape {
    std::vector<float> points; // hull vertices as flat xyz triples (mesh local space); Jolt builds the convex hull
};

struct Collider {
    entt::entity shape = entt::null;
};

struct RigidBody {
    float friction = 0.7f;
    float restitution = 0.05f;
    float damping = 0.05f;
    float mass = 0.0f; // kg; 0 = auto (shape volume x 1000 kg/m^3)
    bool forceSleep = false; // true = keep the body asleep (welds disabled, re-slept every frame);
                             // false = release it (woken once, welds re-enabled, Jolt owns the state)
    std::vector<entt::entity> colliders;
    V3 force{};   // continuous world-space force, applied every tick while non-zero (clear to stop)
    V3 impulse{}; // one-shot: applied on the tick it is seen, then zeroed by the engine

    // forceSleep/force/impulse are commands, not synced state: excluded on purpose
    bool operator==(const RigidBody& o) const {
        return friction == o.friction && restitution == o.restitution && damping == o.damping &&
               mass == o.mass && colliders == o.colliders;
    }
};

struct Joint {
    entt::entity bodyA = entt::null;
    entt::entity bodyB = entt::null;
    int type = 0;
    V3 grabPos{};
    bool excludeCollision = false; // suppress collision between the two joined bodies
};

struct SixDofJoint {
    entt::entity bodyA = entt::null;
    entt::entity bodyB = entt::null; // null = fixed-to-world anchor
    V3 grabPos{};
    bool worldSpace = false; // anchor + frames in world space (default: baked body-local)
    Quat relQuat{1, 0, 0, 0}; // bodyB frame orientation relative to bodyA (identity = coincident)
    // limits, relative to the creation pose (0 = body A at the anchor, in its +axis direction).
    // limOn = false -> all axes free; min > max on an axis fixes it at 0.
    bool limOn = false;
    V3 limTransMin{}, limTransMax{}; // translation limits (m)
    V3 limAngMin{}, limAngMax{}; // rotation limits (rad)
    V3 softTransFreq{}, softTransDamp{}; // soft translation-limit spring (Hz); freq <= 0 = hard
    bool pyramid = false; // rotation-limit shape: false = cone (symmetric), true = pyramid (independent)
    V3 frictionTrans{}, frictionAng{}; // max friction force (N) / torque (N m); 0 = frictionless
    // motors, per axis (6 values: 0-2 translation, 3-5 rotation). state: 0 off, 1 velocity,
    // 2 position, 3 position + velocity; a motor that is off ignores the other motor params.
    // mode: 0 frequency + damping, 1 stiffness + damping, 2 mass-normalized stiffness + damping.
    std::vector<float> motorState; // 6 values
    std::vector<float> motorMode; // 6 values
    V3 motorFreq{}, motorDamp{};
    V3 motorForceMin{}, motorForceMax{}; // translation-motor force limits (N); all-zero = unlimited
    V3 motorTorqueMin{}, motorTorqueMax{}; // rotation-motor torque limits (N m); all-zero = unlimited
    V3 motorTargetPos{}, motorTargetVel{}, motorTargetAngVel{}; // body A displacement / velocity in its +axis direction (0 = creation pose)
    Quat motorTargetQuat{1, 0, 0, 0}; // body A orientation target (identity = creation orientation)
    int priority = 0; // solver priority; higher = solved first
    int velSteps = 0; // solver velocity iterations; 0 = global default
    int posSteps = 0; // solver position iterations; 0 = global default
    bool enabled = true;
    bool excludeCollision = false; // suppress collision between the two joined bodies
};

struct SoftBodyData {
    int pinned = 0;
    float mass = 1.0f;
    float damping = 0.96f;
    int passes = 8;
    float stiffness = 1.0f;
    float tension = 1.0f;
    float friction = 2.0f;
    int steps = kDefaultSteps;
    std::vector<float> cons;
};

struct Environment {
    float intensity = 1.0f;
    entt::entity tex = entt::null; // Ref -> Texture (its GPU-side entity after publish)
};

struct PointLight {
    V3 color{1.0f, 0.97f, 0.92f};
    float intensity = 25.0f;
    float radius = 0.25f;
    float on = 1.0f;
    float shadowNear = 0.01f;
    float shadowFar = 30.0f;
    float shadowNormalBias = 0.05f;
    float shadowBiasBase = 0.3f;
    float shadowBiasSlope = 0.3f;
    float shadowSearchScale = 2.0f;
    float shadowMaxRadius = 16.0f;
};

// marker: the singleton settings entity (RenderSettings + PhysicsSettings components)
struct SettingsRoot {};

struct RenderSettings {
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    float exposure = 1.0f;
};

struct PhysicsSettings {
    int velocitySteps = 10;
    int positionSteps = 10;
};

struct Camera {
    float fovDeg = 50.0f;
    float nearP = 0.01f;
    float farP = 300.0f;
    bool active = false;
    int nViews = 0; // 0 = single view (the entity's Transform), 2 = stereo eyes
    View views[2];
};

enum class TexelFormat {
    RGBA8,
    RGB32F,
};

inline int bytesPerTexel(TexelFormat f) {
    return f == TexelFormat::RGB32F ? 12 : 4;
}

// GPU-side texture: uploaded from a Texture (Renderer::uploadTexture) or produced by a
// render target; the only component Material texture fields may reference
struct GPUTexture {
    uint32_t w = 0, h = 0;
    TexelFormat fmt = TexelFormat::RGBA8;
};

// UI canvas: one ImGui context + one GPU texture + one quad; its Transform is the canvas pose
struct UICanvas {
    V2 worldSize{0.5f, 0.28f}; // full width/height in meters
};

struct ControllerInput {
    int hand = -1;          // 0 left, 1 right
    float triggers[2] = {}; // 0 = trigger, 1 = squeeze (0..1)
    float axis[2] = {};
    bool buttons[2] = {};   // 0 = A, 1 = B (pressed)
    V3 aimPos{};
    Quat aimQuat{1, 0, 0, 0};
    bool active = false;
};

struct KinematicBody {
    std::vector<entt::entity> colliders;
};

struct MouseInput {
    float x = 0.0f;
    float y = 0.0f;
    bool buttons[4] = {};
};

struct KeyboardInput {
    static constexpr int kMaxKeys = 352; // past GLFW_KEY_LAST (349)
    bool keys[kMaxKeys] = {};
};

struct Renderable {
    entt::entity geom = entt::null;
    bool noShadow = false;
    bool visible = true;

    Renderable() = default;
    Renderable(entt::entity g, bool ns = false) : geom(g), noShadow(ns) {}
};

struct Geometry {
    Mesh mesh;
};

struct Skeleton {
    std::string name;
    std::vector<entt::entity> joints;
    std::vector<std::string> boneNames; // joint-index order
    std::vector<Mat4> invBinds;
    entt::entity placement = entt::null;
    std::vector<entt::entity> meshes;
};

struct Texture {
    std::string path;
    TexelFormat fmt = TexelFormat::RGBA8;
    std::string space = "linear"; // "srgb" = data is sRGB encoded; the upload decodes it
    std::vector<uint8_t> data;    // packed: w*h*bytesPerTexel(fmt)
    uint32_t w = 0, h = 0;
};

struct Material {
    V3 baseColor{1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    V3 emissive{0.0f, 0.0f, 0.0f};
    entt::entity tex = entt::null;
    entt::entity texMR = entt::null;
    entt::entity texNormal = entt::null;
    entt::entity texEmissive = entt::null;
    int alphaMode = 0;
    float alphaCutoff = 0.5f;
    float alpha = 1.0f;
    V3 ssssWeight{0.0f, 0.0f, 0.0f}; // per-channel SSS strength: blur distance per color channel (gltf baseColorTransmission)
    float refraction = 0.0f; // 0 = off; the refraction scale (Godot refraction, 0..1)
    float ior = 1.5f;
    float normalScale = 1.0f; // gltf normalTexture.scale; 0 = normal map off
};

struct MaterialRef {
    entt::entity mat;
};

struct Name {
    std::string id;
};

struct UIWindow {
    std::string title;
    entt::entity canvas = entt::null;
    V2 size{320.0f, 200.0f}; // canvas pixel size; the window fills the canvas
    int flags = 0;
    int order = 0;
};

struct UIWidget {
    int order = 0;
};

struct UISliderF {
    std::string label;
    float value = 0.0f;
    float lo = 0.0f, hi = 1.0f;
    std::string fmt = "%.2f";
};

struct UISliderI {
    std::string label;
    int value = 0;
    int lo = 0, hi = 1;
};

struct UICheckbox {
    std::string label;
    bool value = false;
};

struct UIColorEdit {
    std::string label;
    float value[3] = {0.0f, 0.0f, 0.0f};
};

struct UIDragV3 {
    std::string label;
    float value[3] = {0.0f, 0.0f, 0.0f};
    float speed = 0.1f;
};

struct UIText {
    std::string text;
};

enum class BindDir : uint8_t { Both, Read, Write };
struct Bind {
    std::string url;
    std::string target; // "Component.field" on this entity; empty = the widget value field (the UI draw path)
    int dir = static_cast<int>(BindDir::Both); // 0=Both 1=Read 2=Write (int so SliderI can write it safely)
};

struct UIButton {
    std::string label;
    std::string action;
    bool pressed = false;
};

struct UISection {
    std::string label;
    bool collapsible = false;
};

struct UITab {
    std::string label;
};

struct CompField {
    std::string name;
    float v[4] = {};
    int n = 0; // 0 = none, 1 = scalar v[0], 3/4 = small array in v[0..n-1], arrays in arr: n = count
    bool isArr = false;
    std::vector<float> arr;
    bool isStr = false;
    std::string str;
    bool isStrArr = false;
    std::vector<std::string> strArr;
    bool hasEnt = false;
    entt::entity ent = entt::null;
};
using CompFields = std::vector<CompField>;

bool compEmplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err);
bool compReplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err);
bool compGet(const entt::registry& reg, entt::entity e, const std::string& type, CompFields& out);
bool hasComponentByName(const entt::registry& reg, entt::entity e, const std::string& name);
bool compRemove(entt::registry& reg, entt::entity e, const std::string& type, std::string& err);
