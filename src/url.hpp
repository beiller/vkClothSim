#pragma once
// scene URLs: "name.field" addressable state. name = entity Name (or reserved root
// "render"/"rigid"); field = reflect.hpp table entry ("a.b" nested names allowed;
// "name.Component.field" disambiguates).
#include "reflect.hpp"
#include <entt/entt.hpp>
#include <string>

struct World;

struct UrlRef {
    const FieldDesc* field = nullptr;
    void* base = nullptr;
};

bool resolveUrl(const World& w, const std::string& url, UrlRef& out, std::string& err);
// resolve a field on a specific entity (no name lookup); same field rules as resolveUrl
bool resolveEntityField(const World& w, entt::entity ent, const std::string& field, const std::string& label,
                        UrlRef& out, std::string& err);
// n = 1 for scalars, 3 for Color/DragV3
bool sceneSet(World& w, const std::string& url, const float* vals, int n, std::string& err);
// read a URL into vals (maxN floats); out n = 1 for scalars, 3 for Color/DragV3
bool sceneGet(const World& w, const std::string& url, float* vals, int maxN, int& n, std::string& err);
// handle form: address a field on a specific entity (not a name URL)
bool sceneSetEnt(World& w, entt::entity ent, const std::string& field, const float* vals, int n, std::string& err);
bool sceneGetEnt(const World& w, entt::entity ent, const std::string& field, float* vals, int maxN, int& n,
                 std::string& err);
