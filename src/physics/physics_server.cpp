#include "physics/physics_server.hpp"

#include <algorithm>
#include <chrono>

#include "ecs.hpp"
#include "util.hpp"

void PhysicsServer::init() { m_scene.init(); }

// one Jolt step per frame at render rate; dt is clamped so a stall can't produce a huge unstable step
void PhysicsServer::update(entt::registry& reg, float dt) {
    const float pdt = std::min(dt, kMaxPhysDt);
    if (m_hook)
        m_hook(pdt);
    const auto t0 = std::chrono::steady_clock::now();
    m_scene.project(reg);
    for (const auto e : reg.view<KinematicBody>()) {
        const Transform wt = worldTransform(reg, e);
        m_scene.setKinematicPose(e, wt.pos, wt.quat, pdt);
    }
    m_scene.step(reg, pdt);
    reg.view<RigidBody, Transform>().each([&](entt::entity e, auto&, Transform& t) {
        const Transform p = m_scene.pose(e);
        t.pos = p.pos;
        t.quat = p.quat;
    });
    m_joltMs += msBetween(t0, std::chrono::steady_clock::now());
}

RayHit PhysicsServer::raycast(const V3& origin, const V3& dir, float maxDist,
                             const std::vector<entt::entity>& excludes) const {
    return m_scene.raycast(origin, dir, maxDist, excludes);
}
