#include "systems.hpp"

#include "vk/vkapp.hpp"
#include <cmath>
#include <unordered_map>

entt::entity spawnCapsule(World& w, const RigidBody& body, int geom) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Transform>(e, body.position, body.orientation);
    w.reg.emplace<CapsuleCollider>(e, w.sim.addCapsule(body.shape));
    w.reg.emplace<RigidDynamics>(e, body.props, w.rigid.addRigidBody(body));
    w.reg.emplace<Renderable>(e, geom, w.renderer.addInstance(geom));
    return e;
}

entt::entity spawnStaticBody(World& w, const RigidBody& body, int geom) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Transform>(e, body.position, body.orientation);
    w.reg.emplace<RigidStatic>(e, w.rigid.addStaticBody(body));
    w.reg.emplace<CapsuleCollider>(e, w.sim.addCapsule(body.shape));
    w.reg.emplace<Renderable>(e, geom, w.renderer.addInstance(geom));
    return e;
}

entt::entity spawnSoftBody(World& w, const Mesh& mesh, const std::vector<sim::Constraint>& cons, int pinned) {
    entt::entity e = w.reg.create();
    const Renderer::GpuMeshRef ref = w.renderer.addMesh(mesh);
    w.reg.emplace<SoftBodyData>(e, w.sim.addSoftBody(mesh, cons, ref.rw), pinned);
    w.reg.emplace<Renderable>(e, ref.geom, w.renderer.addInstance(ref.geom));
    return e;
}

entt::entity spawnStaticMesh(World& w, const Mesh& mesh, const Transform& t) {
    entt::entity e = w.reg.create();
    const Renderer::GpuMeshRef ref = w.renderer.addMesh(mesh);
    w.reg.emplace<Transform>(e, t.pos, t.quat);
    w.reg.emplace<Renderable>(e, ref.geom, w.renderer.addInstance(ref.geom));
    return e;
}

entt::entity spawnLight(World& w, const V3& pos, const PointLight& p, const char* name) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Transform>(e, pos, V4{0.0f, 0.0f, 0.0f, 1.0f});
    w.reg.emplace<PointLight>(e, p);
    w.reg.emplace<Name>(e, name);
    return e;
}

entt::entity spawnVrCamera(World& w, const char* name) {
    // player rig: world X/Y/Z anchor the viewer stands at (translation-only; set back 5m)
    const V3 kPlayerPos{0.0f, 0.0f, -5.0f};
    // eye height above the rig (standing eye level)
    const V3 kEyeHeight{0.0f, 1.6f, 0.0f};
    entt::entity rig = w.reg.create();
    w.reg.emplace<Transform>(rig, kPlayerPos, V4{0, 0, 0, 1});
    w.reg.emplace<Name>(rig, "player rig");
    entt::entity eye = w.reg.create();
    w.reg.emplace<Transform>(eye, kEyeHeight, V4{0, 0, 0, 1});
    w.reg.emplace<Parent>(eye, rig);
    w.reg.emplace<Name>(eye, "eye height");
    // head: tracks the OpenXR pose (position + rotation); the rig supplies the world placement
    entt::entity head = w.reg.create();
    w.reg.emplace<Transform>(head, V3{0, 0, 0}, V4{0, 0, 0, 1});
    w.reg.emplace<Parent>(head, eye);
    w.reg.emplace<VrCamera>(head);
    w.reg.emplace<Name>(head, name);
    w.playerRig = rig;
    w.eyeRig = eye;
    w.vrCam = head;
    return head;
}

void applyMoveLook(float dt, V3& pos, V4& quat, VkApp& app, double& prevCX, double& prevCY) {
    ImGuiIO& io = ImGui::GetIO();
    double cx, cy;
    glfwGetCursorPos(app.glfwWindow(), &cx, &cy);
    if (glfwGetMouseButton(app.glfwWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !io.WantCaptureMouse) {
        const float sens = 0.0025f;
        const float dx = (float)(cx - prevCX);
        const float dy = (float)(cy - prevCY);
        quat = quatMul(quatAxisAngle({0, 1, 0}, -dx * sens), quatMul(quat, quatAxisAngle({1, 0, 0}, -dy * sens)));
    }
    prevCX = cx;
    prevCY = cy;
    const float qf[4] = {quat.x, quat.y, quat.z, quat.w};
    float rotm[9];
    quatToMat3(qf, rotm);
    const V3 right{rotm[0], rotm[1], rotm[2]};
    const V3 back{rotm[6], rotm[7], rotm[8]};
    V3 mv{0, 0, 0};
    if (!io.WantCaptureKeyboard) {
        if (app.keyIsDown(GLFW_KEY_W))
            mv = vAdd(mv, vScale(back, -1.0f));
        if (app.keyIsDown(GLFW_KEY_S))
            mv = vAdd(mv, back);
        if (app.keyIsDown(GLFW_KEY_D))
            mv = vAdd(mv, right);
        if (app.keyIsDown(GLFW_KEY_A))
            mv = vAdd(mv, vScale(right, -1.0f));
        if (app.keyIsDown(GLFW_KEY_E))
            mv = vAdd(mv, V3{0, 1, 0});
        if (app.keyIsDown(GLFW_KEY_Q))
            mv = vAdd(mv, V3{0, -1, 0});
    }
    if (vLen(mv) > 0.0f)
        pos = vAdd(pos, vScale(vNorm(mv), (app.keyIsDown(GLFW_KEY_LEFT_SHIFT) ? 6.0f : 2.0f) * dt));
}

void stepXr(World& w, float dt) {
    const XrFrame& fr = w.xrFrame;
    if (fr.havePose && fr.nEyes > 0) {
        // head tracks the XR pose (LOCAL ref space, relative to session start)
        const V3 mid = vScale(vAdd(fr.eye[0].pos, fr.eye[fr.nEyes - 1].pos), 0.5f);
        for (auto [entity, t] : w.reg.view<Transform, VrCamera>().each()) {
            t.pos = mid;
            t.quat = fr.eye[0].quat;
        }
    } else {
        // no session: virtual head driven by mouse/keyboard
        VirtHead& vh = w.ui.virtHead;
        if (!vh.init) {
            vh.pos = w.camera.position;
            vh.quat = w.camera.rotation;
            vh.init = true;
        }
        static double prevCX = 0.0, prevCY = 0.0;
        applyMoveLook(dt, vh.pos, vh.quat, *w.app, prevCX, prevCY);
    }
    XrEyeData eyes[2]{};
    int n = 2;
    if (fr.havePose && fr.nEyes > 0) {
        // the player rig (X/Y/Z anchor + eye height) places the head in the scene
        n = fr.nEyes;
        V3 rigPos{0, 0, 0};
        if (w.eyeRig != entt::null)
            if (const auto* wt = w.reg.try_get<WorldTransform>(w.eyeRig))
                rigPos = wt->pos;
        for (int i = 0; i < n; ++i) {
            eyes[i] = fr.eye[i];
            eyes[i].pos = vAdd(eyes[i].pos, rigPos);
        }
    } else {
        const V3 head = w.ui.virtHead.pos;
        const V4 q = w.ui.virtHead.quat;
        const float qf[4] = {q.x, q.y, q.z, q.w};
        float rotm[9];
        quatToMat3(qf, rotm);
        const V3 right{rotm[0], rotm[1], rotm[2]};
        for (int i = 0; i < 2; ++i) {
            eyes[i].pos = vAdd(head, vScale(right, i == 0 ? -0.032f : 0.032f));
            eyes[i].quat = q;
            // Quest 3 lens tangents for the window debug view
            eyes[i].tanL = -std::tan(54.0f * kPi / 180.0f);
            eyes[i].tanR = std::tan(40.0f * kPi / 180.0f);
            eyes[i].tanU = std::tan(44.0f * kPi / 180.0f);
            eyes[i].tanD = -std::tan(55.0f * kPi / 180.0f);
        }
    }
    w.renderer.setVrEyes(eyes, n);
}

void stepPinHolds(World& w, float dt) {
    for (auto [entity, hold, sb] : w.reg.view<PinHold, SoftBodyData>().each()) {
        hold.time += dt;
        if (hold.time >= hold.holdTime)
            sb.pinned = 0;
    }
}

void resetSofts(World& w) {
    for (auto [entity, sb] : w.reg.view<SoftBodyData>().each())
        w.sim.resetSoft(sb.softId);
    for (auto [entity, hold, sb] : w.reg.view<PinHold, SoftBodyData>().each()) {
        hold.time = 0.0f;
        sb.pinned = 1;
    }
}

void stepRigid(World& w) {
    w.rigid.step();
    for (auto [entity, dyn, t] : w.reg.view<RigidDynamics, Transform>().each())
        t = w.rigid.pose(dyn.id);
}

void stepStaticRigid(World& w) {
    for (auto [entity, st, t] : w.reg.view<RigidStatic, Transform>().each())
        w.rigid.setStaticPose(st.id, t);
}

void stepAnimation(World& w, float dt) {
    w.reg.view<Animation, Transform>().each([&](auto, Animation& a, Transform& t) { a.fn(t, dt); });
}

void resolveWorldTransforms(World& w) {
    auto& reg = w.reg;
    struct Node {
        entt::entity e;
        Transform local;
        entt::entity parent;
    };
    std::vector<Node> nodes;
    for (auto [e, t] : reg.view<Transform>().each()) {
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
    for (int i = 0; i < n; ++i)
        if (done[i])
            reg.emplace_or_replace<WorldTransform>(nodes[i].e, world[i]);
}

void syncColliders(World& w) {
    for (auto [entity, col, wt] : w.reg.view<CapsuleCollider, WorldTransform>().each())
        w.sim.setCapsulePose(col.slot, wt);
}

void stepSoft(World& w, VkCommandBuffer cmd) {
    for (auto [entity, sb] : w.reg.view<SoftBodyData>().each()) {
        w.sim.setPinned(sb.softId, sb.pinned);
        w.sim.setParams(sb.softId, sb.params, sb.steps);
    }
    w.sim.record(cmd);
}

void syncSceneToRenderer(World& w) {
    for (auto [entity, rend, wt] : w.reg.view<Renderable, WorldTransform>().each())
        w.renderer.setModel(rend.inst, wt.toMat4());
    for (auto [entity, rend] : w.reg.view<Renderable>().each()) {
        const auto* mat = w.reg.try_get<Material>(entity);
        w.renderer.setMaterial(rend.inst, mat ? mat->baseColor : V3{1.0f, 1.0f, 1.0f}, mat ? mat->metallic : 0.0f,
                               mat ? mat->roughness : 0.5f);
    }
    std::vector<Renderer::Light> lights;
    for (auto [entity, wt, p] : w.reg.view<WorldTransform, PointLight>().each())
        lights.push_back({wt.pos, p});
    w.renderer.setLights(lights);
    w.renderer.setEnvIntensity(w.ui.envIntensity);
}

void draw(World& w, VkCommandBuffer cmd, uint32_t fb, const float bg[3], ImDrawData* imgui) {
    syncSceneToRenderer(w);
    w.renderer.draw(cmd, *w.app, fb, bg, imgui, w.ui.exposure);
}
