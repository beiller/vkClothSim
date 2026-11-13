#pragma once
#include "math.hpp"
#include "mesh.hpp"
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

struct VrCamera {};

struct VrRoot {};

struct LuaScript {
    std::string file;
    int mod = -1;
    int inst = -1;
    int params = -1;
    int onReady = -1;
    int animate = -1;
    int physics = -1;
    bool ready = false;
    // C++ mirror of the script module's top-level fields: URL bind targets ("name.LuaScript.var").
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
    float halfHeight = 0.0f;
    float bottomRadius = 0.0f;
    float topRadius = 0.0f;
};

struct SphereShape {
    float radius = 0.0f;
};

struct ConvexHullShape {
    std::vector<float> points;
};

struct Collider {
    entt::entity shape = entt::null;
};

struct RigidBody {
    float friction = 0.7f;
    float restitution = 0.05f;
    float damping = 0.05f;
    float mass = 0.0f;
    bool forceSleep = false;
    std::vector<entt::entity> colliders;
    V3 force{};
    V3 impulse{};

    bool operator==(const RigidBody& o) const {
        return friction == o.friction && restitution == o.restitution && damping == o.damping && mass == o.mass &&
               colliders == o.colliders;
    }
};

struct Joint {
    entt::entity bodyA = entt::null;
    entt::entity bodyB = entt::null;
    int type = 0;
    V3 grabPos{};
    bool excludeCollision = false;
};

struct SixDofJoint {
    entt::entity bodyA = entt::null;
    entt::entity bodyB = entt::null;
    V3 grabPos{};
    bool worldSpace = false;
    Quat relQuat{1, 0, 0, 0};
    Quat axisQuat{1, 0, 0, 0};
    bool limOn = false;
    V3 limTransMin{}, limTransMax{};
    V3 limAngMin{}, limAngMax{};
    V3 softTransFreq{}, softTransDamp{};
    bool pyramid = false;
    V3 frictionTrans{}, frictionAng{};
    std::vector<float> motorState;
    std::vector<float> motorMode;
    V3 motorFreq{}, motorDamp{};
    V3 motorForceMin{}, motorForceMax{};
    V3 motorTorqueMin{}, motorTorqueMax{};
    V3 motorTargetPos{}, motorTargetVel{}, motorTargetAngVel{};
    Quat motorTargetQuat{1, 0, 0, 0};
    int priority = 0;
    int velSteps = 0;
    int posSteps = 0;
    bool enabled = true;
    bool excludeCollision = false;
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
    entt::entity tex = entt::null;
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

struct SettingsRoot {};

struct RenderSettings {
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    float exposure = 1.0f;
    float sssDepthReject = 2.0f;
    float sssCurvScale = 1.0f;
    float renderScale = 1.0f; // scene buffer size vs display size (window, or headset rect in VR)
};

struct PhysicsSettings {
    int velocitySteps = 500;
    int positionSteps = 500;
};

struct Camera {
    float fovDeg = 50.0f;
    float nearP = 0.01f;
    float farP = 300.0f;
    bool active = false;
    int nViews = 0;
    View views[2];
};

enum class TexelFormat : uint8_t {
    RGBA8,
    RGB32F,
};

struct GPUTexture {
    uint32_t w = 0, h = 0;
    TexelFormat fmt = TexelFormat::RGBA8;
};

struct UICanvas {
    V2 worldSize{0.5f, 0.28f};
};

struct ControllerInput {
    int hand = -1;
    float triggers[2] = {};
    float axis[2] = {};
    bool buttons[2] = {};
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
    static constexpr int kMaxKeys = 352;
    bool keys[kMaxKeys] = {};
};

struct Renderable {
    entt::entity geom = entt::null;
    bool noShadow = false;
    bool visible = true;

    Renderable() = default;
    explicit Renderable(entt::entity g, bool ns = false) : geom(g), noShadow(ns) {}
};

struct Geometry {
    Mesh mesh;
};

struct Skeleton {
    std::string name;
    std::vector<entt::entity> joints;
    std::vector<std::string> boneNames;
    std::vector<Mat4> invBinds;
    entt::entity placement = entt::null;
    std::vector<entt::entity> meshes;
};

struct CompReg;

struct AnimationClip {
    float duration = 0.0f;            // computed on write when 0: max last keyframe time
    std::vector<std::string> targets; // one full URL per track: "name.Component.field"
    std::vector<float> interps;       // per track: 0=linear 1=step 2=cubic (empty = linear)
    std::vector<float> trackStart;    // N+1 keyframe offsets into times
    std::vector<float> times;         // concatenated in track order, ascending per track
    std::vector<float> values;        // concatenated; segment = count * field width
    std::vector<float> tangents;      // cubic only: 2*(n-1)*width per track [outT0, inT1, ...]
};

struct AnimationPlayer {
    entt::entity clip = entt::null;
    float time = 0.0f, speed = 1.0f;
    int loop = 1;         // 0=once(clamp), 1=loop, 2=pingpong
    bool playing = false; // paused by default: imported clips start only when explicitly started
};

struct Texture {
    std::string path;
    TexelFormat fmt = TexelFormat::RGBA8;
    std::string space = "linear";
    std::vector<uint8_t> data;
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
    V3 ssssWeight{0.0f, 0.0f, 0.0f};
    float refraction = 0.0f;
    float ior = 1.5f;
    float normalScale = 1.0f;
    float ssssScale = 0.15f;
    float ssssSplit = 0.333f;
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
    V2 size{320.0f, 200.0f};
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
    std::string change_url;
};

struct UISliderI {
    std::string label;
    int value = 0;
    int lo = 0, hi = 1;
    std::string change_url;
};

struct UICheckbox {
    std::string label;
    bool value = false;
    std::string change_url;
};

struct UIColorEdit {
    std::string label;
    float value[3] = {0.0f, 0.0f, 0.0f};
    std::string change_url;
};

struct UIDragV3 {
    std::string label;
    float value[3] = {0.0f, 0.0f, 0.0f};
    float speed = 0.1f;
    std::string change_url;
};

struct UIText {
    std::string label;
};

enum class BindDir : uint8_t { Both, Read, Write };
struct Bind {
    std::string url;
    std::string target;
    int dir = static_cast<int>(BindDir::Both);
};

struct UIButton {
    std::string label;
    std::string target_url;
    bool pressed = false;
};

struct UISection {
    std::string label;
    bool collapsible = false;
};

struct UITab {
    std::string label;
};

struct TypeDesc;

struct CompReg {
    const char* name;
    const TypeDesc* desc;
    void* (*tryGet)(const entt::registry&, entt::entity);
    void (*emplace)(entt::registry&, entt::entity);
    void (*remove)(entt::registry&, entt::entity);
    bool (*afterWrite)(entt::registry&, entt::entity, std::string&);
};

struct CompField {
    std::string name;
    float v[4] = {};
    int n = 0;
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
bool hasComponentByName(const entt::registry& reg, entt::entity e, const std::string& name);
const CompReg* compFor(const std::string& name);
// Name is not a registered CompReg (set directly via emplace), so route all name writes here:
// they invalidate the URL cache, since a changed name remaps every cached target.
void setName(entt::registry& reg, entt::entity e, const std::string& id);

bool compWriteField(entt::registry& reg, entt::entity e, const std::string& typeName, const CompField& f,
                    std::string& err);
bool compRemove(entt::registry& reg, entt::entity e, const std::string& type, std::string& err);
