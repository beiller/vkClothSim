#include "physics/physics_server.hpp"

#include <chrono>

#include "ecs.hpp"

static float msBetween(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<float, std::milli>(b - a).count();
}

void PhysicsServer::init() { m_scene.init(); }

void PhysicsServer::update(entt::registry& reg, float dt) {
    m_acc += dt;
    if (m_acc > 0.4f)
        m_acc = 0.4f;
    while (m_acc >= kFrameDt) {
        if (m_hook)
            m_hook(kFrameDt);
        const auto t0 = std::chrono::steady_clock::now();
        m_scene.project(reg);
        for (const auto e : reg.view<KinematicBody>()) {
            const Transform wt = worldTransform(reg, e);
            m_scene.setKinematicPose(e, wt.pos, wt.quat, kFrameDt);
        }
        m_scene.step(reg, kFrameDt);
        reg.view<RigidBody, Transform>().each([&](entt::entity e, auto&, Transform& t) {
            const Transform p = m_scene.pose(e);
            t.pos = p.pos;
            t.quat = p.quat;
        });
        m_joltMs += msBetween(t0, std::chrono::steady_clock::now());
        m_acc -= kFrameDt;
    }
}

RayHit PhysicsServer::raycast(const V3& origin, const V3& dir, float maxDist,
                             const std::vector<entt::entity>& excludes) const {
    return m_scene.raycast(origin, dir, maxDist, excludes);
}
