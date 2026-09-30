#include "systems.hpp"

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
    entt::entity e = w.reg.create();
    w.reg.emplace<Transform>(e, V3{0.0f, 1.6f, 3.0f}, V4{0.0f, 0.0f, 0.0f, 1.0f});
    w.reg.emplace<VrCamera>(e);
    w.reg.emplace<Name>(e, name);
    return e;
}

#ifdef WITH_OPENXR
void syncVrCamera(World& w, const XrFrameData& fr) {
    for (auto [e, t, vr] : w.reg.view<Transform, VrCamera>().each()) {
        if (fr.havePose && fr.nEyes > 0) {
            const V3 mid = vScale(vAdd(fr.eye[0].pos, fr.eye[fr.nEyes - 1].pos), 0.5f);
            t.pos = vAdd(mid, vr.rigPos);
            t.quat = fr.eye[0].quat;
        } else {
            t.pos = vAdd(V3{0.0f, 1.6f, 3.0f}, vr.rigPos);
            t.quat = V4{0.0f, 0.0f, 0.0f, 1.0f};
        }
    }
}
#endif

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
    w.reg.view<Animation, Transform>().each([&](auto, Animation& a, Transform& t) {
        a.fn(t, dt);
    });
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
