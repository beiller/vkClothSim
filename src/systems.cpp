#include "systems.hpp"

#include "assets.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

void addMesh(Engine& w, entt::entity e, entt::entity geom, entt::entity mat, entt::entity parent, bool noShadow) {
    if (!w.reg.try_get<Transform>(e))
        w.reg.emplace<Transform>(e); // identity: a mesh with no explicit pose sits at the origin
    w.reg.emplace<Renderable>(e, geom, noShadow);
    w.reg.emplace<MaterialRef>(e, mat);
    if (parent != entt::null)
        w.reg.emplace<Parent>(e, parent);
}

entt::entity spawnMaterial(Engine& w, const V3& baseColor, float metallic, float roughness, const V3& emissive,
                           const char* name) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Material>(e, baseColor, metallic, roughness, emissive);
    if (name)
        w.reg.emplace<Name>(e, name);
    return e;
}


entt::entity textureBytes(Engine& w, const std::string& id, const uint8_t* rgba, int wpx, int hpx) {
    for (auto [e, tx] : w.reg.view<Texture>().each())
        if (tx.path == id)
            return e;
    entt::entity e = w.reg.create();
    w.renderer.addTexture(e, rgba, (uint32_t)wpx, (uint32_t)hpx);
    w.reg.emplace<Texture>(e, id);
    w.reg.emplace<Name>(e, id);
    return e;
}

entt::entity findActiveCamera(const Engine& w) {
    entt::entity cam = entt::null;
    w.reg.view<Camera>().each([&](entt::entity e, const Camera& c) {
        if (c.active)
            cam = e;
    });
    return cam;
}

// the VR root's world translation (stick locomotion); {0,0,0} when there is no VR root
static V3 walkPos(const Engine& w) {
    return w.vrRoot != entt::null ? w.reg.get<Transform>(w.vrRoot).pos : V3{0, 0, 0};
}

CameraView cameraView(Engine& w, entt::entity cam, float aspect) {
    const Transform wt = worldTransform(w.reg, cam);
    const Camera& c = w.reg.get<Camera>(cam);
    const Mat4 view = viewFromPose(wt.pos, wt.quat);
    const float tanU = std::tan(c.fovDeg * 0.5f * kPi / 180.0f);
    const float tanR = tanU * aspect;
    return {mul4(perspective(c.fovDeg, aspect, c.nearP, c.farP), view), view, wt.pos, -tanR, tanR, tanU, -tanU,
            c.nearP, c.farP};
}

void stepXr(Engine& w, const XrFrame& fr) {
    const bool havePose = fr.havePose && fr.nEyes > 0;
    // current camera: the VR head camera while presenting, the scene's fly camera otherwise
    if (w.vrCam != entt::null)
        w.reg.get<Camera>(w.vrCam).active = havePose;
    if (w.cam != entt::null)
        w.reg.get<Camera>(w.cam).active = !havePose;
    const entt::entity cam = findActiveCamera(w);
    if (cam == entt::null)
        return;
    Camera& c = w.reg.get<Camera>(cam);
    if (havePose) {
        // the VR head camera tracks the XR pose: a child of the VR root, so its LOCAL Transform is
        // the head pose and its world = walkPos + head; the per-eye views (camera data, not
        // entities) get walkPos added directly
        const V3 mid = vScale(vAdd(fr.eye[0].pos, fr.eye[fr.nEyes - 1].pos), 0.5f);
        Transform& t = w.reg.get<Transform>(cam);
        t.pos = mid;
        t.quat = fr.eye[0].quat;
        const V3 walk = walkPos(w);
        c.nViews = fr.nEyes;
        for (int i = 0; i < c.nViews; ++i) {
            c.views[i] = fr.eye[i];
            c.views[i].pos = vAdd(c.views[i].pos, walk);
        }
    } else {
        // no session: virtual head = the fly camera's world pose, with Quest 3 lens tangents
        const Transform wt = worldTransform(w.reg, cam);
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

// natural resting spot (head-local: -Z forward, +Y up) so the grip is always visible even
// before/without a controller pose; a real pose overrides this
static V3 restPose(int hand) {
    return hand == 0 ? V3{-0.30f, -0.40f, 0.40f} : V3{0.30f, -0.40f, 0.40f};
}

entt::entity spawnVrRoot(Engine& w) {
    // head-independent locomotion anchor: its world Transform is walkPos, a translation driven
    // only by stick locomotion, so head/grip world = walkPos + pose (head motion can't leak into
    // the grips); the vr cam is C++-driven (XR pose) instead of script-driven
    entt::entity root = w.reg.create();
    w.reg.emplace<Transform>(root, V3{0, 0, 0}, V4{0, 0, 0, 1});
    w.reg.emplace<Name>(root, "vr root");
    entt::entity head = w.reg.create();
    w.reg.emplace<Transform>(head, V3{0, 0, 0}, V4{0, 0, 0, 1});
    w.reg.emplace<Camera>(head, Camera{90.0f, 0.01f, 300.0f, false});
    w.reg.emplace<Parent>(head, root);
    w.reg.emplace<Name>(head, "vr cam");
    w.vrCam = head;
    w.vrRoot = root;
    return root;
}

void spawnControllers(Engine& w) {
    // grips are children of the VR root (LOCAL = the raw OpenXR grip pose, set each frame, so
    // world = walkPos + grip). Grip visual from the std geometry library; the engine gives hands
    // no physics body - a scene script may add one (a KinematicBody + colliders)
    std::string err;
    const entt::entity geom = w.script.callMeshGen("capsule", {{"halfLen", 0.06}, {"radius", 0.03}}, err);
    if (geom == entt::null) {
        std::fprintf(stderr, "3dsim: controller grip geometry: %s\n", err.c_str());
        std::exit(1);
    }
    const entt::entity gripMat = spawnMaterial(w, V3{0.9f, 0.75f, 0.2f}, 0.0f, 0.4f, V3{0.0f, 0.0f, 0.0f});
    for (int h = 0; h < 2; ++h) {
        entt::entity e = w.reg.create();
        w.reg.emplace<Transform>(e, V3{0, 0, 0}, V4{0, 0, 0, 1}); // local rel. to vr root; per frame
        if (w.vrRoot != entt::null)
            w.reg.emplace<Parent>(e, w.vrRoot);
        w.reg.emplace<ControllerInput>(e, ControllerInput{h});
        w.reg.emplace<Renderable>(e, geom);
        w.reg.emplace<MaterialRef>(e, gripMat);
        w.reg.emplace<Name>(e, h == 0 ? "controller L" : "controller R");
    }
}

void stepControllers(Engine& w, const XrFrame& fr) {
    const entt::entity cam = findActiveCamera(w);
    Transform headLocal{}; // head pose in the vr root's frame (the grips' parent)
    if (cam != entt::null) {
        if (fr.havePose && fr.nEyes > 0)
            headLocal = w.reg.get<Transform>(cam); // vr cam: the XR head pose, already local
        else {
            // virtual head: the fly camera's world pose, re-expressed under the (rooted) vr root
            const Transform wt = worldTransform(w.reg, cam);
            headLocal = Transform{vSub(wt.pos, walkPos(w)), wt.quat};
        }
    }
    w.reg.view<ControllerInput, Transform>().each([&](auto e, ControllerInput& ci, Transform& t) {
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

void stepRigid(Engine& w) {
    // new body/joint components project first, so a script that created them this step sees them
    // bound before the step integrates
    w.rigid.project(w.reg);
    // kinematic bodies are driven from the ECS: pull each one's world pose into Jolt before the
    // step so this step integrates against the pose that was just written
    for (const auto e : w.reg.view<KinematicBody>()) {
        const Transform wt = worldTransform(w.reg, e);
        w.rigid.setKinematicPose(e, wt.pos, wt.quat);
    }
    w.rigid.step();
    w.reg.view<RigidBody, Transform>().each([&](entt::entity e, auto&, Transform& t) {
        // Jolt poses are rigid: preserve the entity's scale
        const Transform p = w.rigid.pose(e);
        t.pos = p.pos;
        t.quat = p.quat;
    });
}

void resolveWorldTransforms(Engine& w) {
    auto& reg = w.reg;
    struct Node {
        entt::entity e;
        Transform local;
        entt::entity parent;
    };
    std::vector<Node> nodes;
    for (auto [e, t] : reg.view<Transform>().each()) {
        if (reg.try_get<SoftBodyData>(e))
            continue; // soft-body geometry is world-space (pinned pose lifted at spawn)
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
    w.gpuSim.soft.syncCapsules(w); // the capsule list tracks the bodies' world poses this step
    // new SoftBodyData components project first (geometry upload + sim buffers), so a body
    // created this step simulates on this step
    w.gpuSim.soft.project(w);
    for (auto [entity, sb] : w.reg.view<SoftBodyData>().each()) {
        w.gpuSim.soft.setPinned(entity, sb.pinned);
        w.gpuSim.soft.setParams(entity, sb.params, sb.steps);
    }
    w.gpuSim.soft.record(cmd);
}

void draw(Engine& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui) {
    w.renderer.draw(cmd, *w.app, fb, w.reg, w.render, imgui);
}
