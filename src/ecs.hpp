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

    Mat4 toMat4() const {
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

struct WorldTransform : Transform {};

struct Parent {
    entt::entity e;
};

inline Transform transformCompose(const Transform& parent, const Transform& local) {
    Transform r;
    r.quat = quatMul(parent.quat, local.quat);
    r.scale = {parent.scale.x * local.scale.x, parent.scale.y * local.scale.y, parent.scale.z * local.scale.z};
    r.pos = vAdd(parent.pos,
                 quatRotate(parent.quat,
                            V3{local.pos.x * parent.scale.x, local.pos.y * parent.scale.y, local.pos.z * parent.scale.z}));
    return r;
}

inline Transform transformInvert(const Transform& t) {
    const V3 r = quatRotate(quatInv(t.quat), t.pos);
    return {{-r.x / t.scale.x, -r.y / t.scale.y, -r.z / t.scale.z},
            quatInv(t.quat),
            {1.0f / t.scale.x, 1.0f / t.scale.y, 1.0f / t.scale.z}};
}

struct LuaScript {
    std::string file;
    int inst = -1;
    int params = -1;
    int onReady = -1;
    int animate = -1;
    int physics = -1;
    bool ready = false;
};

struct CapsuleShape {
    float halfLen = 0.0f;
    float radius = 0.0f;
};

struct BoxShape {
    V3 half{};
};

struct Collider {
    entt::entity shape = entt::null;
};

struct RigidBody {
    float friction = 0.7f;
    float restitution = 0.05f;
    float damping = 0.05f;
    bool isStatic = false;
    std::vector<entt::entity> colliders;
};

struct Joint {
    entt::entity bodyA = entt::null;
    entt::entity bodyB = entt::null;
    int type = 0;
    V3 grabPos{};
};

struct SoftBodyData {
    int pinned = 0;
    SimParams params;
    int steps = kDefaultSteps;
    std::vector<float> cons;
};

struct Camera {
    float fovDeg = 50.0f;
    float nearP = 0.01f;
    float farP = 300.0f;
    bool active = false;
    int nViews = 0; // 0 = single view (the entity's Transform), 2 = stereo eyes
    View views[2];
};

struct ControllerInput {
    int hand = -1;          // 0 left, 1 right
    float triggers[2] = {}; // 0 = trigger, 1 = squeeze (0..1)
    float axis[2] = {};
    bool buttons[2] = {};   // 0 = A, 1 = B (pressed)
    V3 aimPos{};
    V4 aimQuat{0, 0, 0, 1};
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
    std::vector<Mat4> invBinds;
    entt::entity placement = entt::null;
    std::vector<entt::entity> meshes;
};

struct Texture {
    std::string path;
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
    float ssssWeight = 0.0f; // 0 = off; the SSS strength scalar (Godot subsurface_scattering_strength)
    float refraction = 0.0f; // 0 = off; the refraction scale (Godot refraction, 0..1)
    float ior = 1.5f;
};

struct MaterialRef {
    entt::entity mat;
};

struct Name {
    std::string id;
};

inline entt::entity findNamedEntity(const entt::registry& reg, const std::string& name) {
    for (auto [e, n] : reg.view<Name>().each())
        if (n.id == name)
            return e;
    return entt::null;
}

inline Transform worldTransform(const entt::registry& reg, entt::entity e) {
    Transform acc{};
    bool have = false;
    while (e != entt::null) {
        const Transform& t = reg.get<Transform>(e);
        acc = have ? transformCompose(t, acc) : t;
        have = true;
        const auto* p = reg.try_get<Parent>(e);
        e = p ? p->e : entt::null;
    }
    return acc;
}

struct UIWindow {
    std::string title;
    std::function<V2()> pos;
    V2 size{};
    int flags = 0;
    bool collapseOnAppear = false;
    int order = 0;
};

struct UIWidget {
    int order = 0;
};

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
    std::string label;
    std::function<std::string()> text;
};

struct UIButton {
    std::string label;
    std::function<void()> on;
};

struct UISection {
    std::string label;
    bool collapsible = false;
};

struct UITab {
    std::string label;
};
