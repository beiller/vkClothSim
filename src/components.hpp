#pragma once
#include <entt/entt.hpp>
#include <string>
#include <vector>

// component construction in the struct-form field vocabulary ("pos", "quat", "color", ...) -
// one core for the JSON loader and the Lua scene.* verbs. Pure-data components are
// table-driven over reflect.hpp; subsystem components (Jolt bodies, GPU soft bodies, renderer
// instances) keep their domain verbs for now and join this vocabulary later as constructor
// hooks on the same registry (the verbs would call the same CompFields interface).
struct CompField {
    std::string name;
    float v[4] = {};
    int n = 0; // 1 = scalar, 3 = vec3, 4 = quat
};
using CompFields = std::vector<CompField>;

// create; error if already present
bool compEmplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err);
// create-or-assign; unspecified fields keep current/default values
bool compReplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err);
// read a component's fields (struct form); false if the entity lacks it
bool compGet(entt::registry& reg, entt::entity e, const std::string& type, CompFields& out);
// remove; pure-data components only
bool compRemove(entt::registry& reg, entt::entity e, const std::string& type, std::string& err);
