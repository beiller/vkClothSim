#pragma once
#include <entt/entt.hpp>
#include <string>
#include <vector>

struct CompField {
    std::string name;
    float v[4] = {};
    int n = 0; // 0 = none, 1 = scalar v[0], 3/4 = small array in v[0..n-1], arrays in arr: n = count
    bool isArr = false;
    std::vector<float> arr;
    bool isStr = false;
    std::string str;
    bool hasEnt = false;
    entt::entity ent = entt::null;
};
using CompFields = std::vector<CompField>;

bool compEmplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err);
bool compReplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err);
bool compGet(const entt::registry& reg, entt::entity e, const std::string& type, CompFields& out);
bool compRemove(entt::registry& reg, entt::entity e, const std::string& type, std::string& err);
