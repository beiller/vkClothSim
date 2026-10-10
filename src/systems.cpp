#include "systems.hpp"

#include "meshgen.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdio>
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

entt::entity spawnCamera(World& w, const V3& pos, const V4& quat, float fovDeg, bool active) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Transform>(e, pos, quat);
    Camera c;
    c.fovDeg = fovDeg;
    c.active = active;
    w.reg.emplace<Camera>(e, c);
    return e;
}

entt::entity findActiveCamera(const World& w) {
    entt::entity cam = entt::null;
    w.reg.view<Camera>().each([&](entt::entity e, const Camera& c) {
        if (c.active)
            cam = e;
    });
    return cam;
}

// the VR root's world translation (stick locomotion); {0,0,0} when there is no VR root
static V3 walkPos(const World& w) {
    return w.vrRoot != entt::null ? w.reg.get<Transform>(w.vrRoot).pos : V3{0, 0, 0};
}

Transform worldTransform(entt::registry& reg, entt::entity e) {
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

Mat4 cameraViewProj(World& w, entt::entity cam, float aspect) {
    const Transform wt = worldTransform(w.reg, cam);
    const Camera& c = w.reg.get<Camera>(cam);
    return mul4(perspective(c.fovDeg, aspect, c.nearP, c.farP), viewFromPose(wt.pos, wt.quat));
}

static void applyMoveLook(float dt, V3& pos, V4& quat, VkApp& app, FlyCam& fc) {
    ImGuiIO& io = ImGui::GetIO();
    double cx, cy;
    glfwGetCursorPos(app.glfwWindow(), &cx, &cy);
    if (glfwGetMouseButton(app.glfwWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !io.WantCaptureMouse) {
        const float sens = 0.0025f;
        const float dx = (float)(cx - fc.prevCX);
        const float dy = (float)(cy - fc.prevCY);
        quat = quatMul(quatAxisAngle({0, 1, 0}, -dx * sens), quatMul(quat, quatAxisAngle({1, 0, 0}, -dy * sens)));
    }
    fc.prevCX = cx;
    fc.prevCY = cy;
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

void stepFlyCamera(World& w, float dt) {
    // the active camera is the fly camera, or the virtual head while the XR session is not running
    if (w.xrFrame.havePose && w.xrFrame.nEyes > 0)
        return;
    const entt::entity cam = findActiveCamera(w);
    if (cam == entt::null)
        return;
    Transform& t = w.reg.get<Transform>(cam);
    applyMoveLook(dt, t.pos, t.quat, *w.app, w.flyCam);
}

void stepXr(World& w) {
    const XrFrame& fr = w.xrFrame;
    const entt::entity cam = findActiveCamera(w);
    XrEyeData eyes[2]{};
    int n = 0;
    if (fr.havePose && fr.nEyes > 0 && cam != entt::null) {
        // head tracks the XR pose. The camera is a child of the VR root, so its LOCAL Transform is
        // the reported head pose and its world = walkPos + head. The per-eye render poses are data
        // (not entities), so they get walkPos added directly.
        const V3 mid = vScale(vAdd(fr.eye[0].pos, fr.eye[fr.nEyes - 1].pos), 0.5f);
        Transform& t = w.reg.get<Transform>(cam);
        t.pos = mid;
        t.quat = fr.eye[0].quat;
        const V3 walk = walkPos(w);
        n = fr.nEyes;
        for (int i = 0; i < n; ++i) {
            eyes[i] = fr.eye[i];
            eyes[i].pos = vAdd(eyes[i].pos, walk);
        }
    } else if (cam != entt::null) {
        // no session: virtual head = the active camera's world pose, with Quest 3 lens tangents
        const Transform wt = worldTransform(w.reg, cam);
        const V3 head = wt.pos;
        const V4 q = wt.quat;
        const float qf[4] = {q.x, q.y, q.z, q.w};
        float rotm[9];
        quatToMat3(qf, rotm);
        const V3 right{rotm[0], rotm[1], rotm[2]};
        for (int i = 0; i < 2; ++i) {
            eyes[i].pos = vAdd(head, vScale(right, i == 0 ? -0.032f : 0.032f));
            eyes[i].quat = q;
            eyes[i].tanL = -std::tan(54.0f * kPi / 180.0f);
            eyes[i].tanR = std::tan(40.0f * kPi / 180.0f);
            eyes[i].tanU = std::tan(44.0f * kPi / 180.0f);
            eyes[i].tanD = -std::tan(55.0f * kPi / 180.0f);
        }
        n = 2;
    }
    if (n > 0)
        w.renderer.setVrEyes(eyes, n);
}

// natural resting spot (head-local: -Z forward, +Y up) so the grip is always visible even
// before/without a controller pose; a real pose overrides this
static V3 restPose(int hand) {
    return hand == 0 ? V3{-0.30f, -0.40f, 0.40f} : V3{0.30f, -0.40f, 0.40f};
}

entt::entity spawnVrRoot(World& w, entt::entity head) {
    // head-independent locomotion anchor. The head (active camera) and the grips are parented
    // under it; its world Transform is walkPos, a translation driven only by stick locomotion
    // (identity rotation), never the head. So head/grip world = walkPos + pose, and head motion
    // can't leak into the grips (no drift).
    entt::entity root = w.reg.create();
    w.reg.emplace<Transform>(root, V3{0, 0, 0}, V4{0, 0, 0, 1});
    w.reg.emplace<Name>(root, "vr root");
    if (head != entt::null)
        w.reg.emplace_or_replace<Parent>(head, root);
    w.vrRoot = root;
    return root;
}

void spawnControllers(World& w) {
    // grips are children of the VR root; their LOCAL Transform is set to the raw OpenXR grip each
    // frame, so world = walkPos + grip (head-independent -> no drift).
    const CapsuleParams gripShape{0.06f, 0.03f};
    const int geom = w.renderer.addMesh(makeCapsuleMesh(gripShape, 1)).geom;
    for (int h = 0; h < 2; ++h) {
        entt::entity e = w.reg.create();
        w.reg.emplace<Transform>(e, V3{0, 0, 0}, V4{0, 0, 0, 1}); // local rel. to vr root; per frame
        if (w.vrRoot != entt::null)
            w.reg.emplace<Parent>(e, w.vrRoot);
        w.reg.emplace<ControllerInput>(e, ControllerInput{h, {}, {0, 0, 0, 1}, 0.0f, false});
        if (h == 1)
            w.reg.emplace<Grab>(e); // the right hand drives the grab
        w.reg.emplace<Renderable>(e, geom, w.renderer.addInstance(geom));
        w.reg.emplace<Material>(e, V3{0.9f, 0.75f, 0.2f}, 0.0f, 0.4f);
        w.reg.emplace<Name>(e, h == 0 ? "controller L" : "controller R");
    }
}

void stepControllers(World& w) {
    const XrFrame& fr = w.xrFrame;
    const entt::entity cam = findActiveCamera(w);
    // grips are children of the VR root. Their LOCAL Transform is the raw OpenXR grip (so world
    // = walkPos + grip, head-independent -> no drift); the missing-hand rest pose goes behind
    // the head using the head's LOCAL pose (relative to the same root).
    Transform headLocal{};
    if (cam != entt::null)
        headLocal = w.reg.get<Transform>(cam);
    w.reg.view<ControllerInput, Transform>().each([&](auto e, ControllerInput& ci, Transform& t) {
        if (ci.hand < 0 || ci.hand >= 2)
            return;
        const XrController& c = fr.controller[ci.hand];
        const bool on = fr.running && c.active;
        ci.active = on;
        ci.aimPos = c.aimPos;
        ci.aimQuat = c.aimQuat;
        ci.trigger = c.trigger;
        if (on)
            t = Transform{c.pos, c.quat}; // grip as reported (local rel. to the vr root)
        else if (cam != entt::null)
            t = transformCompose(headLocal, Transform{restPose(ci.hand), V4{0, 0, 0, 1}}); // behind and below the head
    });
}

// throw velocity imparted on release, per-frame blend of the aim motion into the throw, aim-ray length
constexpr float kThrowImpulse = 1.5f;
constexpr float kThrowLerp = 0.2f;
constexpr float kGrabRange = 3.0f;

void stepGrab(World& w, float dt) {
    const XrFrame& fr = w.xrFrame;
    // the right hand is the only controller with a Grab component
    const ControllerInput* rc = nullptr;
    Grab* grab = nullptr;
    for (auto [e, ci, g] : w.reg.view<ControllerInput, Grab>().each()) {
        rc = &ci;
        grab = &g;
    }
    auto release = [&] {
        if (grab && grab->active) {
            w.rigid.setVelocity(grab->body, vScale(grab->throwVel, kThrowImpulse));
            grab->active = false;
        }
    };
    if (!fr.running || !rc || !rc->active) {
        release();
        return;
    }

    // aim point in world = walkPos + the OpenXR aim pose (the aim pose is in the reference space,
    // like the grips). walkPos is head-independent, so this stays drift-free.
    const V3 walk = walkPos(w);
    const Transform aimWorld{vAdd(rc->aimPos, walk), rc->aimQuat};
    const V3 fwd = vNorm(quatRotate(aimWorld.quat, V3{0, 0, -1})); // -Z of the aim pose

    if (grab->active) {
        w.rigid.moveBody(grab->body, vAdd(aimWorld.pos, grab->offset), quatMul(aimWorld.quat, grab->offsetQuat));
        if (dt > 0.0f)
            grab->throwVel = vAdd(vScale(grab->throwVel, 1.0f - kThrowLerp),
                                  vScale(vSub(aimWorld.pos, grab->prevAim), kThrowLerp / dt));
        grab->prevAim = aimWorld.pos;
    }

    const bool held = rc->trigger > 0.5f;
    if (held && !grab->active) {
        const RayHit hit = w.rigid.raycast(aimWorld.pos, fwd, kGrabRange);
        if (hit.hit && w.rigid.isDynamic(hit.body)) {
            const Transform bp = w.rigid.pose(hit.body);
            grab->body = hit.body;
            grab->offset = vSub(bp.pos, aimWorld.pos);
            grab->offsetQuat = quatMul(bp.quat, quatInv(aimWorld.quat));
            grab->throwVel = {0, 0, 0};
            grab->prevAim = aimWorld.pos;
            grab->active = true;
        }
    } else if (!held && grab->active) {
        release();
    }
}

void stepLocomotion(World& w, float dt) {
    const XrFrame& fr = w.xrFrame;
    if (w.vrRoot == entt::null || !fr.havePose || fr.nEyes == 0 || dt <= 0.0f)
        return;
    const XrController& rc = fr.controller[1]; // right hand drives locomotion
    if (!rc.stickOn) // stick source must be bound; independent of the (sometimes flaky) pose actions
        return;
    const float kSpeed = 2.0f; // m/s
    const float kDead = 0.12f; // stick deadzone
    const float sx = rc.stick.x, sy = rc.stick.y;
    if (sx * sx + sy * sy < kDead * kDead)
        return;
    // horizontal facing: rotate the world -Z (forward) and +X (right) by the head quaternion,
    // then flatten to the floor plane (pitch/roll ignored)
    const V4 hq = fr.eye[0].quat;
    V3 fwd = quatRotate(hq, V3{0, 0, -1});
    V3 rgt = quatRotate(hq, V3{1, 0, 0});
    fwd = vNorm({fwd.x, 0.0f, fwd.z});
    rgt = vNorm({rgt.x, 0.0f, rgt.z});
    Transform& rt = w.reg.get<Transform>(w.vrRoot);
    const V3 move = vAdd(vScale(fwd, sy), vScale(rgt, sx));
    rt.pos = vAdd(rt.pos, vScale(move, kSpeed * dt));
    rt.pos.y = 0.0f; // walk on the floor plane (head height comes from the STAGE pose)
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
    for (auto [entity, g] : w.reg.view<MaterialGroup>().each())
        w.reg.get<Material>(entity) = w.reg.get<Material>(g.leader);
    for (auto [entity, rend, wt] : w.reg.view<Renderable, WorldTransform>().each())
        w.renderer.setModel(rend.inst, wt.toMat4());
    for (auto [entity, rend] : w.reg.view<Renderable>().each()) {
        const auto* mat = w.reg.try_get<Material>(entity);
        w.renderer.setMaterial(rend.inst, mat ? mat->baseColor : V3{1.0f, 1.0f, 1.0f}, mat ? mat->metallic : 0.0f,
                               mat ? mat->roughness : 0.5f);
    }
    w.lights.clear();
    for (auto [entity, wt, p] : w.reg.view<WorldTransform, PointLight>().each())
        w.lights.push_back({wt.pos, p});
    w.renderer.setLights(w.lights);
    w.renderer.setEnvIntensity(w.render.envIntensity);
}

void draw(World& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui) {
    syncSceneToRenderer(w);
    w.renderer.draw(cmd, *w.app, fb, w.render.bgColor, imgui, w.render.exposure);
}
