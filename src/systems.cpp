#include "systems.hpp"

#include "assets.hpp"
#include "ecs_ops.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

entt::entity findActiveCamera(const entt::registry& reg) {
    entt::entity cam = entt::null;
    reg.view<Camera>().each([&](entt::entity e, const Camera& c) {
        if (c.active)
            cam = e;
    });
    return cam;
}

static V3 walkPos(const entt::registry& reg) {
    const entt::entity root = findMarker<VrRoot>(reg);
    return root != entt::null ? reg.get<Transform>(root).pos : V3{0, 0, 0};
}

CameraView cameraView(const entt::registry& reg, entt::entity cam, float aspect) {
    const Transform wt = worldTransform(reg, cam);
    const Camera& c = reg.get<Camera>(cam);
    const Mat4 view = viewFromPose(wt.pos, wt.quat);
    const float tanU = std::tan(c.fovDeg * 0.5f * kPi / 180.0f);
    const float tanR = tanU * aspect;
    return {mul4(perspective(c.fovDeg, aspect, c.nearP, c.farP), view), view, wt.pos, -tanR, tanR, tanU, -tanU,
            c.nearP, c.farP};
}

void stepXr(entt::registry& reg, const XrFrame& fr) {
    const bool havePose = fr.havePose && fr.nEyes > 0;
    const entt::entity vrCam = findMarker<VrCamera>(reg);
    if (vrCam != entt::null)
        reg.get<Camera>(vrCam).active = havePose;
    const entt::entity cam = findMarker<SceneCamera>(reg);
    if (cam != entt::null)
        reg.get<Camera>(cam).active = !havePose;
    const entt::entity active = findActiveCamera(reg);
    if (active == entt::null)
        return;
    Camera& c = reg.get<Camera>(active);
    if (havePose) {
        const V3 mid = vScale(vAdd(fr.eye[0].pos, fr.eye[fr.nEyes - 1].pos), 0.5f);
        Transform& t = reg.get<Transform>(active);
        t.pos = mid;
        t.quat = fr.eye[0].quat;
        const V3 walk = walkPos(reg);
        c.nViews = fr.nEyes;
        for (int i = 0; i < c.nViews; ++i) {
            c.views[i] = fr.eye[i];
            c.views[i].pos = vAdd(c.views[i].pos, walk);
        }
    } else {
        const Transform wt = worldTransform(reg, active);
        const V3 head = wt.pos;
        const V4 q = wt.quat;
        const float qf[4] = {q.x, q.y, q.z, q.w};
        float rotm[9];
        quatToMat3(qf, rotm);
        const V3 right{rotm[0], rotm[1], rotm[2]};
        c.nViews = 2;
        for (int i = 0; i < 2; ++i)
            c.views[i] = View{vAdd(head, vScale(right, i == 0 ? -0.032f : 0.032f)), q,
                             -std::tan(54.0f * kPi / 180.0f), std::tan(40.0f * kPi / 180.0f),
                             std::tan(44.0f * kPi / 180.0f), -std::tan(55.0f * kPi / 180.0f)};
    }
}

static V3 restPose(int hand) {
    return hand == 0 ? V3{-0.30f, -0.40f, 0.40f} : V3{0.30f, -0.40f, 0.40f};
}

entt::entity spawnVrRoot(entt::registry& reg) {
    entt::entity root = reg.create();
    reg.emplace<Transform>(root, V3{0, 0, 0}, V4{0, 0, 0, 1});
    reg.emplace<VrRoot>(root);
    reg.emplace<Name>(root, "vr root");
    entt::entity head = reg.create();
    reg.emplace<Transform>(head, V3{0, 0, 0}, V4{0, 0, 0, 1});
    reg.emplace<Camera>(head, Camera{90.0f, 0.01f, 300.0f, false});
    reg.emplace<VrCamera>(head);
    reg.emplace<Parent>(head, root);
    reg.emplace<Name>(head, "vr cam");
    return root;
}

void spawnControllers(Engine& w) {
    std::string err;
    const entt::entity geom = w.script.callMeshGen("capsule", {{"halfLen", 0.06}, {"radius", 0.03}}, err);
    if (geom == entt::null) {
        std::fprintf(stderr, "3dsim: controller grip geometry: %s\n", err.c_str());
        std::exit(1);
    }
    const entt::entity gripMat = spawnMaterial(w.reg, V3{0.9f, 0.75f, 0.2f}, 0.0f, 0.4f, V3{0.0f, 0.0f, 0.0f});
    for (int h = 0; h < 2; ++h) {
        entt::entity e = w.reg.create();
        w.reg.emplace<Transform>(e, V3{0, 0, 0}, V4{0, 0, 0, 1});
        if (const entt::entity root = findMarker<VrRoot>(w.reg); root != entt::null)
            w.reg.emplace<Parent>(e, root);
        w.reg.emplace<ControllerInput>(e, ControllerInput{h});
        w.reg.emplace<Renderable>(e, geom);
        w.reg.emplace<MaterialRef>(e, gripMat);
        w.reg.emplace<Name>(e, h == 0 ? "controller L" : "controller R");
    }
}

void stepControllers(entt::registry& reg, const XrFrame& fr) {
    const entt::entity cam = findActiveCamera(reg);
    Transform headLocal{};
    if (cam != entt::null) {
        if (fr.havePose && fr.nEyes > 0)
            headLocal = reg.get<Transform>(cam);
        else {
            const Transform wt = worldTransform(reg, cam);
            headLocal = Transform{vSub(wt.pos, walkPos(reg)), wt.quat};
        }
    }
    reg.view<ControllerInput, Transform>().each([&](auto e, ControllerInput& ci, Transform& t) {
        if (ci.hand < 0 || ci.hand >= 2)
            return;
        const XrController& c = fr.controller[ci.hand];
        const bool on = fr.running && c.active;
        ci.active = on;
        ci.triggers[0] = c.triggers[0];
        ci.triggers[1] = c.triggers[1];
        ci.axis[0] = c.axis[0];
        ci.axis[1] = c.axis[1];
        ci.buttons[0] = c.buttons[0];
        ci.buttons[1] = c.buttons[1];
        ci.aimPos = c.aimPos;
        ci.aimQuat = c.aimQuat;
        if (on)
            t = Transform{c.pos, c.quat};
        else if (cam != entt::null)
            t = transformCompose(headLocal, Transform{restPose(ci.hand), V4{0, 0, 0, 1}});
    });
}

void spawnInputEntities(Engine& w) {
    entt::entity m = w.reg.create();
    w.reg.emplace<MouseInput>(m);
    w.reg.emplace<Name>(m, "mouse");
    entt::entity k = w.reg.create();
    w.reg.emplace<KeyboardInput>(k);
    w.reg.emplace<Name>(k, "keyboard");
}

void stepMouse(Engine& w) {
    auto* app = w.app;
    if (!app)
        return;
    auto& reg = w.reg;
    double cx, cy;
    glfwGetCursorPos(app->glfwWindow(), &cx, &cy);
    ImGuiIO& io = ImGui::GetIO();
    const int btns[4] = {GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE,
                         GLFW_MOUSE_BUTTON_4};
    reg.view<MouseInput>().each([&](auto, MouseInput& mi) {
        mi.x = (float)cx;
        mi.y = (float)cy;
        for (int i = 0; i < 4; ++i)
            mi.buttons[i] = glfwGetMouseButton(app->glfwWindow(), btns[i]) == GLFW_PRESS && !io.WantCaptureMouse;
    });
}

void stepKeyboard(Engine& w) {
    auto* app = w.app;
    if (!app)
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard)
        return;
    auto& reg = w.reg;
    reg.view<KeyboardInput>().each([&](auto, KeyboardInput& ki) {
        for (int k = 0; k < KeyboardInput::kMaxKeys; ++k)
            ki.keys[k] = glfwGetKey(app->glfwWindow(), k) == GLFW_PRESS;
    });
}

void stepRigid(RigidScene& rigid, entt::registry& reg) {
    rigid.project(reg);
    for (const auto e : reg.view<KinematicBody>()) {
        const Transform wt = worldTransform(reg, e);
        rigid.setKinematicPose(e, wt.pos, wt.quat);
    }
    rigid.step();
    reg.view<RigidBody, Transform>().each([&](entt::entity e, auto&, Transform& t) {
        const Transform p = rigid.pose(e);
        t.pos = p.pos;
        t.quat = p.quat;
    });
}

void resolveWorldTransforms(entt::registry& reg) {
    struct Node {
        entt::entity e;
        Transform local;
        entt::entity parent;
    };
    std::vector<Node> nodes;
    for (auto [e, t] : reg.view<Transform>().each()) {
        if (reg.try_get<SoftBodyData>(e))
            continue;
        entt::entity parent = entt::null;
        if (const auto* p = reg.try_get<Parent>(e))
            parent = p->e;
        nodes.push_back({e, t, parent});
    }
    const int n = (int)nodes.size();
    if (n == 0)
        return;
    std::unordered_map<entt::entity, int> idx;
    idx.reserve(n);
    for (int i = 0; i < n; ++i)
        idx[nodes[i].e] = i;
    std::vector<Transform> world(n);
    std::vector<char> done(n, 0);
    for (int i = 0; i < n; ++i)
        if (nodes[i].parent == entt::null) {
            world[i] = nodes[i].local;
            done[i] = 1;
        }
    for (int pass = 0; pass < n; ++pass) {
        bool any = false;
        for (int i = 0; i < n; ++i) {
            if (done[i])
                continue;
            auto it = idx.find(nodes[i].parent);
            if (it != idx.end() && done[it->second]) {
                world[i] = transformCompose(world[it->second], nodes[i].local);
                done[i] = 1;
                any = true;
            }
        }
        if (!any)
            break;
    }
    bool all = true;
    for (int i = 0; i < n; ++i) {
        if (!done[i])
            all = false;
        else
            reg.emplace_or_replace<WorldTransform>(nodes[i].e, world[i]);
    }
    if (!all) {
        static bool s_cycleLogged = false;
        if (!s_cycleLogged) {
            std::fprintf(stderr, "3dsim: parent cycle detected; entities in the cycle have no WorldTransform\n");
            s_cycleLogged = true;
        }
    }
}

void stepSoft(Engine& w, VkCommandBuffer cmd) {
    w.gpuSim.soft.syncCapsules(w.reg);
    w.gpuSim.soft.project(w.reg);
    for (auto [entity, sb] : w.reg.view<SoftBodyData>().each()) {
        w.gpuSim.soft.setPinned(entity, sb.pinned);
        w.gpuSim.soft.setParams(entity, sb.params, sb.steps);
    }
    w.gpuSim.soft.record(cmd);
}

void draw(Engine& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui) {
    const entt::entity p = findUiPanel(w.reg);
    Transform panelT;
    const Transform* pp = nullptr;
    if (p != entt::null) {
        panelT = worldTransform(w.reg, p);
        pp = &panelT;
    }
    w.renderer.draw(cmd, *w.app, fb, w.reg, w.render, imgui, pp);
}

entt::entity findUiPanel(const entt::registry& reg) {
    for (auto e : reg.view<UICanvas>())
        return e;
    return entt::null;
}

entt::entity ensureUiPanel(entt::registry& reg) {
    const entt::entity found = findUiPanel(reg);
    if (found != entt::null)
        return found;
    const entt::entity panel = reg.create();
    reg.emplace<UICanvas>(panel);
    reg.emplace<Transform>(panel);
    const entt::entity shape = reg.create();
    reg.emplace<BoxShape>(shape, BoxShape{V3{kPanelW * 0.5f, kPanelH * 0.5f, 0.01f}});
    const entt::entity col = reg.create();
    reg.emplace<Collider>(col, Collider{shape});
    reg.emplace<KinematicBody>(panel, KinematicBody{{col}});
    return panel;
}

namespace {
V4 panelLookQuat(const V3& panelPos, const V3& atPos) {
    const V3 z = vNormalize(vSub(atPos, panelPos)); // +z faces the viewer
    V3 up{0, 1, 0};
    if (std::fabs(vDot(up, z)) > 0.99f)
        up = {0, 0, 1};
    const V3 x = vNormalize(vCross(up, z));
    const V3 y = vCross(z, x);
    return basisToQuat(x, y, z);
}
} // namespace

bool uiPanelUv(const entt::registry& reg, entt::entity panel, const V3& hitPos, V2& uv) {
    if (panel == entt::null)
        return false;
    const Transform& t = reg.get<Transform>(panel);
    const V3 xh = quatRotate(t.quat, V3{1, 0, 0});
    const V3 yh = quatRotate(t.quat, V3{0, 1, 0});
    const float u = 0.5f + vDot(vSub(hitPos, t.pos), xh) / (kPanelW);
    const float v = 0.5f + vDot(vSub(hitPos, t.pos), yh) / (kPanelH);
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
        return false;
    uv = {u, v}; // v up-positive (1 = panel top), as the ImGui feed expects
    return true;
}
