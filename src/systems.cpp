#include "systems.hpp"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include "assets.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

void addMesh(World& w, entt::entity e, int geom, entt::entity mat, entt::entity parent) {
    if (!w.reg.try_get<Transform>(e))
        w.reg.emplace<Transform>(e); // identity: a mesh with no explicit pose sits at the origin
    w.reg.emplace<Renderable>(e, w.renderer.addInstance(geom));
    w.reg.emplace<MaterialRef>(e, mat);
    if (parent != entt::null)
        w.reg.emplace<Parent>(e, parent);
}

// one collider child entity: local Transform + Collider(shape) + Parent -> the body
entt::entity addCollider(World& w, entt::entity body, entt::entity shape, const Transform& local) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Transform>(e, local);
    w.reg.emplace<Collider>(e, shape);
    w.reg.emplace<Parent>(e, body);
    return e;
}

// the Jolt projection of a shape entity: built on first use, held by the component
static const JPH::Shape* shapeHandle(World& w, entt::entity e) {
    if (auto* cs = w.reg.try_get<CapsuleShape>(e)) {
        if (cs->shape == nullptr)
            cs->shape = JPH::ShapeRefC(new JPH::CapsuleShape(cs->halfLen, cs->radius));
        return cs->shape.GetPtr();
    }
    if (auto* bs = w.reg.try_get<BoxShape>(e)) {
        if (bs->shape == nullptr)
            bs->shape = JPH::ShapeRefC(new JPH::BoxShape(JPH::Vec3(bs->half.x, bs->half.y, bs->half.z)));
        return bs->shape.GetPtr();
    }
    return nullptr;
}

static std::vector<ColliderDesc> shapeDescs(World& w, const std::vector<ColliderSpec>& specs) {
    std::vector<ColliderDesc> out;
    out.reserve(specs.size());
    for (const ColliderSpec& s : specs)
        out.push_back({shapeHandle(w, s.shape), s.local});
    return out;
}

static void spawnColliders(World& w, entt::entity body, const std::vector<ColliderSpec>& specs) {
    for (const ColliderSpec& s : specs)
        addCollider(w, body, s.shape, s.local);
}

void addRigidBody(World& w, entt::entity e, const std::vector<ColliderSpec>& colliders, bool isStatic) {
    if (colliders.empty())
        return;
    const Transform& t = w.reg.get<Transform>(e);
    RigidBody body{}; // default material params
    body.id = w.rigid.addRigidBody(body, shapeDescs(w, colliders), t.pos, t.quat, isStatic);
    w.reg.emplace<RigidBody>(e, body);
    spawnColliders(w, e, colliders);
}

void addKinematicBody(World& w, entt::entity e, const std::vector<ColliderSpec>& colliders) {
    if (colliders.empty())
        return;
    const Transform& t = w.reg.get<Transform>(e);
    w.reg.emplace<KinematicBody>(e, w.rigid.addKinematicBody(shapeDescs(w, colliders), t.pos, t.quat));
    spawnColliders(w, e, colliders);
}

void addSoftBody(World& w, entt::entity e, const Mesh& mesh, const std::vector<sim::Constraint>& cons, entt::entity mat,
                 float mass) {
    // the entity's Transform is the pinned (spawn) pose: lift the local geometry into world space
    // once here; the sim owns the vertices (world space) from then on
    Mesh m = mesh;
    Transform t{};
    if (w.reg.try_get<Transform>(e))
        t = worldTransform(w.reg, e);
    for (int i = 0; i < m.vertexCount(); ++i) {
        const V3 v = vAdd(quatRotate(t.quat, V3{m.pos[3 * i], m.pos[3 * i + 1], m.pos[3 * i + 2]}), t.pos);
        m.pos[3 * i] = v.x;
        m.pos[3 * i + 1] = v.y;
        m.pos[3 * i + 2] = v.z;
        if (m.nrm.size() == m.pos.size()) {
            const V3 n = quatRotate(t.quat, V3{m.nrm[3 * i], m.nrm[3 * i + 1], m.nrm[3 * i + 2]});
            m.nrm[3 * i] = n.x;
            m.nrm[3 * i + 1] = n.y;
            m.nrm[3 * i + 2] = n.z;
        }
    }
    const Renderer::GpuMeshRef ref = w.renderer.addMesh(m);
    w.reg.emplace<SoftBodyData>(e, w.sim.addSoftBody(m, cons, ref.rw));
    if (mass > 0.0f)
        w.reg.get<SoftBodyData>(e).params.mass = mass;
    w.reg.emplace<Renderable>(e, w.renderer.addInstance(ref.geom));
    w.reg.emplace<MaterialRef>(e, mat);
}

entt::entity spawnMaterial(World& w, const V3& baseColor, float metallic, float roughness, const V3& emissive,
                           const char* name) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Material>(e, baseColor, metallic, roughness, emissive);
    if (name)
        w.reg.emplace<Name>(e, name);
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

bool textureFor(World& w, entt::entity e, const std::string& path) {
    TextureData td;
    if (!loadTexture(exeDir() + "/../assets/" + path, td))
        loadTexture("assets/" + path, td);
    if (td.rgba.empty())
        return false;
    w.reg.emplace<Texture>(e, path, w.renderer.addTexture(td.rgba.data(), (uint32_t)td.w, (uint32_t)td.h));
    return true;
}

entt::entity textureEntity(World& w, const std::string& path) {
    for (auto [e, tx] : w.reg.view<Texture>().each())
        if (tx.path == path)
            return e;
    entt::entity e = w.reg.create();
    if (!textureFor(w, e, path)) {
        w.reg.destroy(e);
        return entt::null;
    }
    w.reg.emplace<Name>(e, path);
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

void stepXr(World& w) {
    const XrFrame& fr = w.xrFrame;
    const bool havePose = fr.havePose && fr.nEyes > 0;
    // current camera: the VR head camera while presenting, the scene's fly camera otherwise
    if (w.vrCam != entt::null)
        w.reg.get<Camera>(w.vrCam).active = havePose;
    if (w.cam != entt::null)
        w.reg.get<Camera>(w.cam).active = !havePose;
    const entt::entity cam = findActiveCamera(w);
    XrEyeData eyes[2]{};
    int n = 0;
    if (havePose && cam != entt::null) {
        // the VR head camera tracks the XR pose. It's a child of the VR root, so its LOCAL Transform
        // is the reported head pose and its world = walkPos + head. The per-eye render poses are data
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
        // no session: virtual head = the fly camera's world pose, with Quest 3 lens tangents
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

entt::entity spawnVrRoot(World& w) {
    // head-independent locomotion anchor. The VR head camera and the grips are parented under it;
    // its world Transform is walkPos, a translation driven only by stick locomotion (identity
    // rotation), never the head. So head/grip world = walkPos + pose, and head motion can't leak
    // into the grips (no drift). The VR head camera is a camera like the scene's fly camera, but
    // C++-driven (XR pose, local to the root) instead of script-driven.
    entt::entity root = w.reg.create();
    w.reg.emplace<Transform>(root, V3{0, 0, 0}, V4{0, 0, 0, 1});
    w.reg.emplace<Name>(root, "vr root");
    entt::entity head = w.reg.create();
    w.reg.emplace<Transform>(head, V3{0, 0, 0}, V4{0, 0, 0, 1});
    w.reg.emplace<Camera>(head, Camera{90.0f, 0.1f, 300.0f, false});
    w.reg.emplace<Parent>(head, root);
    w.reg.emplace<Name>(head, "vr cam");
    w.vrCam = head;
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
    const entt::entity gripMat = spawnMaterial(w, V3{0.9f, 0.75f, 0.2f}, 0.0f, 0.4f, V3{0.0f, 0.0f, 0.0f});
    const entt::entity handShape = w.reg.create();
    w.reg.emplace<CapsuleShape>(handShape, 0.06f, 0.03f); // matches the grip visual
    for (int h = 0; h < 2; ++h) {
        entt::entity e = w.reg.create();
        w.reg.emplace<Transform>(e, V3{0, 0, 0}, V4{0, 0, 0, 1}); // local rel. to vr root; per frame
        if (w.vrRoot != entt::null)
            w.reg.emplace<Parent>(e, w.vrRoot);
        w.reg.emplace<ControllerInput>(e, ControllerInput{h});
        if (h == 1)
            addKinematicBody(w, e, {ColliderSpec{handShape, {}}}); // hand: joints + soft-sim collision
        w.reg.emplace<Renderable>(e, w.renderer.addInstance(geom));
        w.reg.emplace<MaterialRef>(e, gripMat);
        w.reg.emplace<Name>(e, h == 0 ? "controller L" : "controller R");
    }
}

void stepControllers(World& w) {
    const XrFrame& fr = w.xrFrame;
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
    const int btns[4] = {GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE,
                         GLFW_MOUSE_BUTTON_4};
    reg.view<MouseInput>().each([&](auto, MouseInput& mi) {
        mi.x = (float)cx;
        mi.y = (float)cy;
        for (int i = 0; i < 4; ++i)
            mi.buttons[i] = glfwGetMouseButton(app->glfwWindow(), btns[i]) == GLFW_PRESS && !io.WantCaptureMouse;
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
        for (int k = 0; k < KeyboardInput::kMaxKeys; ++k)
            ki.keys[k] = glfwGetKey(app->glfwWindow(), k) == GLFW_PRESS;
    });
}

void stepRigid(World& w) {
    w.rigid.step();
    for (auto [entity, body, t] : w.reg.view<RigidBody, Transform>().each())
        t = w.rigid.pose(body.id);
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

void syncColliders(World& w) {
    CapsuleGPU* caps = w.sim.caps();
    int n = 0;
    for (auto [e, col, wt] : w.reg.view<Collider, WorldTransform>().each()) {
        auto* cs = w.reg.try_get<CapsuleShape>(col.shape); // box shapes are CPU-sim only
        if (!cs || n >= SoftSim::kMaxCapsules)
            continue;
        fillCapsule(caps[n++], wt.pos, wt.quat, cs->halfLen, cs->radius);
    }
    w.sim.uploadCapsules(n);
}

void stepSoft(World& w, VkCommandBuffer cmd) {
    for (auto [entity, sb] : w.reg.view<SoftBodyData>().each()) {
        w.sim.setPinned(sb.softId, sb.pinned);
        w.sim.setParams(sb.softId, sb.params, sb.steps);
    }
    w.sim.record(cmd);
}

void draw(World& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui) {
    w.renderer.draw(cmd, *w.app, fb, w.reg, w.render, imgui);
}
