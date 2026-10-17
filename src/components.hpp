#pragma once
#include <entt/entt.hpp>
#include <string>
#include <vector>

// component construction in the struct-form field vocabulary - one core for the JSON loader
// and the Lua scene.* verbs. Every reflect.hpp type is writable from the vocabulary; the field
// tables (names, kinds, offsets) come from reflect.hpp, so a component and its API surface are
// the same table. Types not in the table (Skeleton, Name, ...) are not writable.
struct CompField {
    std::string name;
    float v[4] = {};
    int n = 0; // 0 = none, 1 = scalar v[0], 3/4 = small array in v[0..n-1], arrays in arr: n = count
    bool isArr = false; // the value was an array (v[0..n-1] when it fits, arr otherwise)
    std::vector<float> arr; // full numeric array value (arrays longer than 4)
    bool isStr = false;     // the value was a string
    std::string str;
    bool hasEnt = false;      // the value was an integer (an entity handle)
    entt::entity ent = entt::null; // valid when hasEnt
};
using CompFields = std::vector<CompField>;

// create; error if already present
bool compEmplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err);
// create-or-assign; unspecified fields keep current/default values
bool compReplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err);
// read a component's fields (struct form); false if the entity lacks it
bool compGet(const entt::registry& reg, entt::entity e, const std::string& type, CompFields& out);
// remove
bool compRemove(entt::registry& reg, entt::entity e, const std::string& type, std::string& err);
