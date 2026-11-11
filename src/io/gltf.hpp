#pragma once
#include "ecs.hpp"
#include <string>
#include <unordered_map>
#include <vector>

entt::entity loadGltf(entt::registry& reg, entt::entity e, const std::string& path, std::string& err);

struct GltfTableData {
    std::vector<std::string> keys;
    std::unordered_map<std::string, std::unordered_map<std::string, CompFields>> nodes;
};

bool gltfToData(const std::string& path, GltfTableData& out, std::string& err);
