#pragma once
#include <entt/entt.hpp>
#include <functional>

#include "physics/rigid.hpp"

// CPU rigid-body server (Jolt). One Jolt step per frame at render rate:
// physics hook (Lua _physics) -> project -> kinematic poses -> Jolt step -> pose writeback.
class PhysicsServer {
public:
    RigidScene& scene() { return m_scene; }
    void init();
    void setPhysicsHook(std::function<void(float)> fn) { m_hook = std::move(fn); }
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
    float m_joltMs = 0.0f;
};
