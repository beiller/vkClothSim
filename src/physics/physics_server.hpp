#pragma once
#include <entt/entt.hpp>
#include <functional>

#include "app/rigid.hpp"

// CPU rigid-body server (Jolt). Owns the 120 Hz fixed-step accumulator; per internal
// step: fixed-step hook (Lua _physics) -> project -> kinematic poses -> Jolt step -> pose writeback.
class PhysicsServer {
public:
    RigidScene& scene() { return m_scene; }
    void init();
    void setFixedStepHook(std::function<void(float)> fn) { m_hook = std::move(fn); }
    void update(entt::registry& reg, float dt);
    RayHit raycast(const V3& origin, const V3& dir, float maxDist,
                   const std::vector<entt::entity>& excludes = {}) const;
    Transform pose(entt::entity e) const { return m_scene.pose(e); }
    entt::entity entityFor(JPH::BodyID id) const { return m_scene.entityFor(id); }
    void reset() { m_scene.reset(); }
    void watch(entt::registry& reg) { m_scene.watch(reg); }
    float takeJoltMs() {
        const float t = m_joltMs;
        m_joltMs = 0.0f;
        return t;
    }

private:
    RigidScene m_scene;
    std::function<void(float)> m_hook;
    float m_acc = 0.0f;
    float m_joltMs = 0.0f;
};
