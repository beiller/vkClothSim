#pragma once
#include "reflect.hpp"
#include <entt/entt.hpp>
#include <string>
#include <vector>

struct UrlRef {
    const FieldDesc* field = nullptr;
    void* base = nullptr;
};

// "name.Component.field" / "render.field" / "rigid.field" resolution; entity names may
// contain dots (the last two dot segments are Component.field). "render.*"/"rigid.*"
// resolve against the settings entity's RenderSettings/PhysicsSettings components.
// "name.LuaScript.var" resolves to a slot in the script's vars mirror (synced by the
// script host): numbers as float, booleans, 3/4-number tables as vec3/quat (subvars like
// "pos.x" are member views); string vars go through scene.get/scene.set instead.
bool resolveUrl(const entt::registry& reg, const std::string& url, UrlRef& out, std::string& err);
bool resolveEntityField(const entt::registry& reg, entt::entity ent, const std::string& field, const std::string& label,
                       UrlRef& out, std::string& err);
bool sceneSet(const entt::registry& reg, const std::string& url, const float* vals, int n, std::string& err);
bool sceneGet(const entt::registry& reg, const std::string& url, float* vals, int maxN, int& n, std::string& err);
bool sceneSetEnt(const entt::registry& reg, entt::entity ent, const std::string& field, const float* vals, int n,
                 std::string& err);
bool sceneGetEnt(const entt::registry& reg, entt::entity ent, const std::string& field, float* vals, int maxN, int& n,
                 std::string& err);
