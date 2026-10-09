#include "systems.hpp"

#include "meshgen.hpp"
#include "vk/vkapp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
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

static void applyMoveLook(float dt, V3& pos, V4& quat, VkApp& app, double& prevCX, double& prevCY) {
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

void stepFlyCamera(World& w, float dt) {
    // the active camera is the fly camera, or the virtual head while the XR session is not running
    if (w.xrFrame.havePose && w.xrFrame.nEyes > 0)
        return;
    const entt::entity cam = findActiveCamera(w);
    if (cam == entt::null)
        return;
    static double prevCX = 0.0, prevCY = 0.0;
    Transform& t = w.reg.get<Transform>(cam);
    applyMoveLook(dt, t.pos, t.quat, *w.app, prevCX, prevCY);
}

void stepXr(World& w) {
    const XrFrame& fr = w.xrFrame;
    const entt::entity cam = findActiveCamera(w);
    XrEyeData eyes[2]{};
    int n = 2;
    if (fr.havePose && fr.nEyes > 0 && cam != entt::null) {
        // head tracks the XR pose. World space == the OpenXR reference space (identity mapping),
        // so the camera (a root, not parented) takes the reported head pose directly.
        const V3 mid = vScale(vAdd(fr.eye[0].pos, fr.eye[fr.nEyes - 1].pos), 0.5f);
        Transform& t = w.reg.get<Transform>(cam);
        t.pos = mid;
        t.quat = fr.eye[0].quat;
        n = fr.nEyes;
        for (int i = 0; i < n; ++i)
            eyes[i] = fr.eye[i];
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
    }
    w.renderer.setVrEyes(eyes, n);
}

// natural resting spot (head-local: -Z forward, +Y up) so the grip is always visible even
// before/without a controller pose; a real pose overrides this
static V3 restPose(int hand) {
    return hand == 0 ? V3{-0.30f, -0.40f, 0.40f} : V3{0.30f, -0.40f, 0.40f};
}

// append one line to the shared XR diagnostic file (same as xr.cpp's)
static void sysDiag(const char* fmt, ...) {
    char buf[256];
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    if (FILE* f = std::fopen("/tmp/3dsim_xr_diag.log", "a")) {
        std::fprintf(f, "%s", buf);
        std::fclose(f);
    }
}

void spawnControllers(World& w, entt::entity head) {
    // The grips are placed directly in world (ref->world) space each frame; they are NOT
    // parented to the head so head motion never affects them (a VR root for navigation is a
    // later addition; for now world space is the reference).
    (void)head;
    const CapsuleParams gripShape{0.06f, 0.03f};
    const int geom = w.renderer.addMesh(makeCapsuleMesh(gripShape, 1)).geom;
    for (int h = 0; h < 2; ++h) {
        entt::entity e = w.reg.create();
        w.reg.emplace<Transform>(e, V3{0.0f, 1.0f, -5.0f}, V4{0, 0, 0, 1}); // world; updated per frame
        w.reg.emplace<ControllerInput>(e, ControllerInput{h, {}, {0, 0, 0, 1}, 0.0f, false});
        w.reg.emplace<Renderable>(e, geom, w.renderer.addInstance(geom));
        w.reg.emplace<Material>(e, V3{0.9f, 0.75f, 0.2f}, 0.0f, 0.4f);
        w.reg.emplace<Name>(e, h == 0 ? "controller L" : "controller R");
    }
}

void stepControllers(World& w) {
    const XrFrame& fr = w.xrFrame;
    const entt::entity cam = findActiveCamera(w);
    // World space == the OpenXR reference space (identity mapping). The grips are roots, placed
    // exactly where OpenXR reports them -- independent of the head, so head motion never moves them.
    Transform headWorld{};
    if (cam != entt::null)
        headWorld = worldTransform(w.reg, cam);
    Transform rightWorld{};
    w.reg.view<ControllerInput, Transform>().each([&](auto, ControllerInput& ci, Transform& t) {
        if (ci.hand < 0 || ci.hand >= 2)
            return;
        const XrController& c = fr.controller[ci.hand];
        const bool on = fr.running && c.active;
        ci.active = on;
        ci.aimPos = c.aimPos;
        ci.aimQuat = c.aimQuat;
        ci.trigger = c.trigger;
        if (on)
            t = Transform{c.pos, c.quat}; // grip exactly as reported (world == ref space)
        else if (cam != entt::null)
            t = transformCompose(headWorld, Transform{restPose(ci.hand), V4{0, 0, 0, 1}}); // in front of head
        if (ci.hand == 1)
            rightWorld = t;
    });
    // throttled diagnostic: rCtrl must equal grip (identity mapping); headW moves with the head
    static int s_diag = 0;
    if (cam != entt::null && (s_diag++ % 30) == 0)
        sysDiag("sys: headW=(%.3f,%.3f,%.3f) rCtrl=(%.3f,%.3f,%.3f) grip=(%.3f,%.3f,%.3f)\n", headWorld.pos.x,
                headWorld.pos.y, headWorld.pos.z, rightWorld.pos.x, rightWorld.pos.y, rightWorld.pos.z,
                fr.controller[1].pos.x, fr.controller[1].pos.y, fr.controller[1].pos.z);
}

void stepGrab(World& w, float dt) {
    Grab& grab = w.grab;
    const XrFrame& fr = w.xrFrame;
    auto release = [&] {
        if (grab.active) {
            w.rigid.setVelocity(grab.body, vScale(grab.throwVel, 1.5f));
            grab.active = false;
        }
    };

    const entt::entity cam = findActiveCamera(w);
    const ControllerInput* rc = nullptr;
    if (cam != entt::null)
        for (auto [e, ci] : w.reg.view<ControllerInput>().each())
            if (ci.hand == 1)
                rc = &ci;
    if (!fr.running || fr.nControllers == 0 || !rc || !rc->active) {
        release();
        return;
    }

    // World space == the OpenXR reference space (identity mapping); the aim point is exactly
    // where OpenXR reports it, independent of the head.
    const Transform aimWorld{rc->aimPos, rc->aimQuat};
    const float q[4] = {aimWorld.quat.x, aimWorld.quat.y, aimWorld.quat.z, aimWorld.quat.w};
    float rm[9];
    quatToMat3(q, rm);
    const V3 fwd = vNorm({-rm[2], -rm[5], -rm[8]}); // -Z of the aim pose

    if (grab.active) {
        w.rigid.moveBody(grab.body, vAdd(aimWorld.pos, grab.offset), quatMul(aimWorld.quat, grab.offsetQuat));
        if (dt > 0.0f)
            grab.throwVel = vAdd(vScale(grab.throwVel, 0.8f), vScale(vSub(aimWorld.pos, grab.prevAim), 0.2f / dt));
        grab.prevAim = aimWorld.pos;
    }

    const bool held = rc->trigger > 0.5f;
    if (held && !grab.active) {
        const RayHit hit = w.rigid.raycast(aimWorld.pos, fwd, 3.0f);
        if (hit.hit && w.rigid.isDynamic(hit.body)) {
            const Transform bp = w.rigid.pose(hit.body);
            grab.body = hit.body;
            grab.offset = vSub(bp.pos, aimWorld.pos);
            grab.offsetQuat = quatMul(bp.quat, quatInv(aimWorld.quat));
            grab.throwVel = {0, 0, 0};
            grab.prevAim = aimWorld.pos;
            grab.active = true;
        }
    } else if (!held && grab.active) {
        release();
    }
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

void drawUi(World& w) {
    auto& reg = w.reg;
    std::unordered_map<entt::entity, std::vector<entt::entity>> kids;
    for (auto [c, wg, p] : reg.view<UIWidget, Parent>().each()) {
        (void)wg;
        kids[p.e].push_back(c);
    }
    std::function<void(entt::entity, bool&)> drawKids;
    drawKids = [&](entt::entity parent, bool& tabOpen) {
        auto it = kids.find(parent);
        if (it == kids.end())
            return;
        auto v = it->second;
        std::sort(v.begin(), v.end(), [&](entt::entity a, entt::entity b) {
            return reg.get<UIWidget>(a).order < reg.get<UIWidget>(b).order;
        });
        // the tab bar belongs to the level that owns the tab children; content-level
        // recursion must not close it
        bool hasTab = false;
        bool itemOpen = false;
        for (entt::entity e : v)
            if (reg.try_get<UITab>(e)) {
                hasTab = true;
                break;
            }
        for (entt::entity e : v) {
            // scope the widget's ID to its entity; sibling sections reuse labels ("metallic", "on", ...)
            // tabs are excluded: BeginTabBar/BeginTabItem manage their own ID pushes, and wrapping
            // them in PushID/PopID makes the ID-stack top (hence the tab ID) depend on selection
            const bool isTab = reg.try_get<UITab>(e) != nullptr;
            if (!isTab)
                ImGui::PushID((int)e);
            if (const auto* s = reg.try_get<UISliderF>(e))
                ImGui::SliderFloat(s->label.c_str(), s->value(), s->lo, s->hi, s->fmt.c_str());
            else if (const auto* s = reg.try_get<UISliderI>(e))
                ImGui::SliderInt(s->label.c_str(), s->value(), s->lo, s->hi);
            else if (const auto* c = reg.try_get<UICheckbox>(e))
                ImGui::Checkbox(c->label.c_str(), c->value());
            else if (const auto* c = reg.try_get<UIColorEdit>(e))
                ImGui::ColorEdit3(c->label.c_str(), c->value());
            else if (const auto* d = reg.try_get<UIDragV3>(e))
                ImGui::DragFloat3(d->label.c_str(), d->value(), d->speed);
            else if (const auto* t = reg.try_get<UIText>(e)) {
                if (t->text)
                    ImGui::TextUnformatted(t->text().c_str());
                else
                    ImGui::TextUnformatted(t->label.c_str());
            } else if (const auto* b = reg.try_get<UIButton>(e)) {
                if (ImGui::Button(b->label.c_str()))
                    b->on();
            } else if (const auto* sec = reg.try_get<UISection>(e)) {
                if (sec->collapsible) {
                    if (ImGui::CollapsingHeader(sec->label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                        drawKids(e, tabOpen);
                } else {
                    ImGui::SeparatorText(sec->label.c_str());
                    drawKids(e, tabOpen);
                }
            } else if (const auto* tab = reg.try_get<UITab>(e)) {
                // BeginTabItem only pushes an ID when selected; EndTabItem pops
                // unconditionally, so it must be called only for opened items
                if (itemOpen) {
                    ImGui::EndTabItem();
                    itemOpen = false;
                } else if (!tabOpen) {
                    ImGui::BeginTabBar("##tabs");
                    tabOpen = true;
                }
                if (ImGui::BeginTabItem(tab->label.c_str())) {
                    itemOpen = true;
                    drawKids(e, tabOpen);
                }
            }
            if (!isTab)
                ImGui::PopID();
        }
        if (hasTab && tabOpen) {
            if (itemOpen)
                ImGui::EndTabItem();
            ImGui::EndTabBar();
            tabOpen = false;
        }
    };
    std::vector<entt::entity> wins;
    reg.view<UIWindow>().each([&](entt::entity e, auto&) { wins.push_back(e); });
    std::sort(wins.begin(), wins.end(), [&](entt::entity a, entt::entity b) {
        return reg.get<UIWindow>(a).order < reg.get<UIWindow>(b).order;
    });
    for (entt::entity win : wins) {
        const UIWindow& cfg = reg.get<UIWindow>(win);
        if (cfg.visible && !cfg.visible())
            continue;
        // NoMove windows (like the fps HUD) track their pos every frame; others are placed once and
        // can be moved/resized freely (state persists in imgui.ini)
        const bool pin = cfg.flags & ImGuiWindowFlags_NoMove;
        ImGui::SetNextWindowPos(ImVec2(cfg.pos().x, cfg.pos().y), pin ? ImGuiCond_Always : ImGuiCond_Appearing);
        if (cfg.size.x > 0.0f && cfg.size.y > 0.0f)
            ImGui::SetNextWindowSize(ImVec2(cfg.size.x, cfg.size.y), ImGuiCond_Appearing);
        if (cfg.collapseOnAppear)
            ImGui::SetNextWindowCollapsed(true, ImGuiCond_Appearing);
        ImGui::Begin(cfg.title.c_str(), nullptr, cfg.flags);
        bool tabOpen = false;
        drawKids(win, tabOpen);
        ImGui::End();
    }
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
    std::vector<Renderer::Light> lights;
    for (auto [entity, wt, p] : w.reg.view<WorldTransform, PointLight>().each())
        lights.push_back({wt.pos, p});
    w.renderer.setLights(lights);
    w.renderer.setEnvIntensity(w.render.envIntensity);
}

void draw(World& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui) {
    syncSceneToRenderer(w);
    w.renderer.draw(cmd, *w.app, fb, w.render.bgColor, imgui, w.render.exposure);
}
