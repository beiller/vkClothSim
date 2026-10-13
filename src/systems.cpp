#include "systems.hpp"

#include "assets.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

entt::entity spawnCapsule(World& w, const RigidBody& body, int geom) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Transform>(e, body.position, body.orientation);
    w.reg.emplace<CapsuleCollider>(e, w.sim.addCapsule(body.shape));
    w.reg.emplace<RigidDynamics>(e, body.props, w.rigid.addRigidBody(body));
    w.reg.emplace<Renderable>(e, geom, w.renderer.addInstance(geom));
    return e;
}

entt::entity spawnSoftBody(World& w, const Mesh& mesh, const std::vector<sim::Constraint>& cons) {
    entt::entity e = w.reg.create();
    const Renderer::GpuMeshRef ref = w.renderer.addMesh(mesh);
    w.reg.emplace<SoftBodyData>(e, w.sim.addSoftBody(mesh, cons, ref.rw));
    w.reg.emplace<Renderable>(e, ref.geom, w.renderer.addInstance(ref.geom));
    return e;
}

entt::entity spawnStaticGeom(World& w, int geom, const Transform& t) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Transform>(e, t.pos, t.quat);
    w.reg.emplace<Renderable>(e, geom, w.renderer.addInstance(geom));
    return e;
}

entt::entity spawnMaterial(World& w, const V3& baseColor, float metallic, float roughness, const char* name) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Material>(e, baseColor, metallic, roughness);
    if (name)
        w.reg.emplace<Name>(e, name);
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

entt::entity findNamedEntity(const World& w, const std::string& name) {
    entt::entity out = entt::null;
    w.reg.view<Name>().each([&](entt::entity e, const Name& n) {
        if (n.id == name)
            out = e;
    });
    return out;
}

entt::entity spawnDynamicCapsule(World& w, int geom, const CapsuleParams& shape, const Transform& t, entt::entity mat) {
    const RigidBody body{shape, t.pos, t.quat, {}};
    const entt::entity e = spawnCapsule(w, body, geom);
    w.reg.emplace<MaterialRef>(e, mat);
    return e;
}

entt::entity spawnVisualCapsule(World& w, int geom, const Transform& t, entt::entity mat, entt::entity parent) {
    const entt::entity e = w.reg.create();
    w.reg.emplace<Transform>(e, t.pos, t.quat);
    if (parent != entt::null)
        w.reg.emplace<Parent>(e, parent);
    w.reg.emplace<Renderable>(e, geom, w.renderer.addInstance(geom));
    w.reg.emplace<MaterialRef>(e, mat);
    return e;
}

entt::entity spawnSoftCloth(World& w, const Mesh& mesh, const std::vector<sim::Constraint>& cons, entt::entity mat) {
    const entt::entity e = spawnSoftBody(w, mesh, cons);
    w.reg.emplace<MaterialRef>(e, mat);
    return e;
}

entt::entity spawnSoftBall(World& w, const Mesh& mesh, const std::vector<sim::Constraint>& cons, entt::entity mat,
                           float mass) {
    const entt::entity e = spawnSoftBody(w, mesh, cons);
    if (mass > 0.0f)
        w.reg.get<SoftBodyData>(e).params.mass = mass;
    w.reg.emplace<MaterialRef>(e, mat);
    return e;
}

int loadAssetTexture(World& w, const std::string& path) {
    TextureData td;
    if (!loadTexture(exeDir() + "/../assets/" + path, td))
        loadTexture("assets/" + path, td);
    if (td.rgba.empty())
        return -1;
    return w.renderer.addTexture(td.rgba.data(), (uint32_t)td.w, (uint32_t)td.h);
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
    // grip visual from the std geometry library (the engine has no mesh generators of its own)
    std::string err;
    const int geom = w.script.callMeshGen("capsule", {{"halfLen", 0.06}, {"radius", 0.03}}, err);
    if (geom < 0) {
        std::fprintf(stderr, "3dsim: controller grip geometry: %s\n", err.c_str());
        std::exit(1);
    }
    const entt::entity gripMat = spawnMaterial(w, V3{0.9f, 0.75f, 0.2f}, 0.0f, 0.4f);
    for (int h = 0; h < 2; ++h) {
        entt::entity e = w.reg.create();
        w.reg.emplace<Transform>(e, V3{0, 0, 0}, V4{0, 0, 0, 1}); // local rel. to vr root; per frame
        if (w.vrRoot != entt::null)
            w.reg.emplace<Parent>(e, w.vrRoot);
        w.reg.emplace<ControllerInput>(e, ControllerInput{h});
        if (h == 1) {
            const JPH::BodyID bid = w.rigid.addKinematicBody(V3{0, 0, 0}, V4{0, 0, 0, 1}, 0.02f);
            w.reg.emplace<KinematicBody>(e, bid);
        }
        w.reg.emplace<Renderable>(e, geom, w.renderer.addInstance(geom));
        w.reg.emplace<MaterialRef>(e, gripMat);
        w.reg.emplace<Name>(e, h == 0 ? "controller L" : "controller R");
    }
}

void stepControllers(World& w) {
    const XrFrame& fr = w.xrFrame;
    const entt::entity cam = findActiveCamera(w);
    Transform headLocal{};
    if (cam != entt::null)
        headLocal = w.reg.get<Transform>(cam);
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
        if (auto* kb = w.reg.try_get<KinematicBody>(e))
            w.rigid.setKinematicPose(kb->id, vAdd(ci.aimPos, walkPos(w)), ci.aimQuat);
    });
}

void spawnInputEntities(World& w) {
    entt::entity m = w.reg.create();
    w.reg.emplace<MouseInput>(m);
    w.reg.emplace<Name>(m, "mouse");
    entt::entity k = w.reg.create();
    w.reg.emplace<KeyboardInput>(k);
    w.reg.emplace<Name>(k, "keyboard");
}

void stepMouse(World& w) {
    auto* app = w.app;
    if (!app)
        return;
    auto& reg = w.reg;
    double cx, cy;
    glfwGetCursorPos(app->glfwWindow(), &cx, &cy);
    ImGuiIO& io = ImGui::GetIO();
    reg.view<MouseInput>().each([&](auto, MouseInput& mi) {
        mi.x = (float)cx;
        mi.y = (float)cy;
        mi.buttons[0] =
            glfwGetMouseButton(app->glfwWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !io.WantCaptureMouse;
        mi.buttons[1] =
            glfwGetMouseButton(app->glfwWindow(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && !io.WantCaptureMouse;
        mi.buttons[2] =
            glfwGetMouseButton(app->glfwWindow(), GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS && !io.WantCaptureMouse;
        mi.buttons[3] =
            glfwGetMouseButton(app->glfwWindow(), GLFW_MOUSE_BUTTON_4) == GLFW_PRESS && !io.WantCaptureMouse;
    });
}

void stepKeyboard(World& w) {
    auto* app = w.app;
    if (!app)
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard)
        return;
    auto& reg = w.reg;
    reg.view<KeyboardInput>().each([&](auto, KeyboardInput& ki) {
        for (int k = 0; k < 352; ++k)
            ki.keys[k] = glfwGetKey(app->glfwWindow(), k) == GLFW_PRESS;
    });
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
    for (auto [entity, rend, wt] : w.reg.view<Renderable, WorldTransform>().each())
        w.renderer.setModel(rend.inst, wt.toMat4());
    for (auto [entity, rend, ref] : w.reg.view<Renderable, MaterialRef>().each()) {
        if (const auto* mat = w.reg.try_get<Material>(ref.mat)) {
            w.renderer.setMaterial(rend.inst, mat->baseColor, mat->metallic, mat->roughness);
            if (mat->texId >= 0)
                w.renderer.setTexture(rend.inst, 0, mat->texId);
        }
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
