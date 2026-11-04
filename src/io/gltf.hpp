#pragma once
#include "ecs.hpp"
#include <string>
#include <unordered_map>
#include <vector>

entt::entity loadGltf(entt::registry& reg, entt::entity e, const std::string& path, std::string& err);

// parsed glb as a name->node map (node = component-name -> fields); key order in `keys`
struct GltfTableData {
    std::vector<std::string> keys;
    std::unordered_map<std::string, std::unordered_map<std::string, CompFields>> nodes;
};

// registry-free glb parse for the table API: one entry per node (Geometry on the node),
// materials/textures as separate resource keys; embedded image pixels are registered in
// the embedded-texture store
bool gltfToData(const std::string& path, GltfTableData& out, std::string& err);
