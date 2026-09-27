#include "systems.hpp"

entt::entity spawnCapsule(World& w, const RigidBody& body, int geom) {
    entt::entity e = w.reg.create();
    w.reg.emplace<Transform>(e, body.position, body.orientation);
    w.reg.emplace<CapsuleCollider>(e, w.sim.addCapsule(body.shape));
    w.reg.emplace<RigidDynamics>(e, body.props, w.rigid.addRigidBody(body));
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

void syncColliders(World& w) {
    for (auto [entity, col, t] : w.reg.view<CapsuleCollider, Transform>().each())
        w.sim.setCapsulePose(col.slot, t);
}

void stepSoft(World& w, VkCommandBuffer cmd) {
    for (auto [entity, sb] : w.reg.view<SoftBodyData>().each()) {
        w.sim.setPinned(sb.softId, sb.pinned);
        w.sim.setParams(sb.softId, sb.params, sb.steps);
    }
    w.sim.record(cmd);
}

void draw(World& w, VkCommandBuffer cmd, uint32_t fb, const float bg[3], ImDrawData* imgui) {
    for (auto [entity, rend, t] : w.reg.view<Renderable, Transform>().each())
        w.renderer.setModel(rend.inst, t.toMat4());
    for (auto [entity, rend] : w.reg.view<Renderable>().each()) {
        const auto* mat = w.reg.try_get<Material>(entity);
        w.renderer.setMaterial(rend.inst, mat ? mat->baseColor : V3{1.0f, 1.0f, 1.0f}, mat ? mat->metallic : 0.0f,
                               mat ? mat->roughness : 0.5f);
    }
    w.renderer.draw(cmd, *w.app, fb, bg, imgui, w.ui.exposure);
}
