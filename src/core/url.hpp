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
// contain dots (the last two dot segments are Component.field); roots = the non-entity
// reflection roots (type + address), supplied by the caller so this stays Engine-free
bool resolveUrl(const entt::registry& reg, const std::vector<refl::RootRef>& roots, const std::string& url, UrlRef& out,
                std::string& err);
bool resolveEntityField(const entt::registry& reg, entt::entity ent, const std::string& field, const std::string& label,
                       UrlRef& out, std::string& err);
bool sceneSet(const entt::registry& reg, const std::vector<refl::RootRef>& roots, const std::string& url, const float* vals,
              int n, std::string& err);
bool sceneGet(const entt::registry& reg, const std::vector<refl::RootRef>& roots, const std::string& url, float* vals,
              int maxN, int& n, std::string& err);
bool sceneSetEnt(const entt::registry& reg, entt::entity ent, const std::string& field, const float* vals, int n,
                 std::string& err);
bool sceneGetEnt(const entt::registry& reg, entt::entity ent, const std::string& field, float* vals, int maxN, int& n,
                 std::string& err);
