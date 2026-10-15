#pragma once
#include "world.hpp"
#include <string>

// Load a glTF 2.0 (.gltf / .glb) file under entity e: scene nodes become an entity hierarchy
// (Transform + Parent), mesh primitives become child mesh entities (Renderable + MaterialRef).
// glTF materials and images become shared Material/Texture entities (named, deduped globally by
// id; embedded images dedupe as "file#imgN"). Node scale (TRS or matrix) is baked into the
// primitive geometry; skinned or morphed primitives are skipped with a warning.
// Returns e on success; on failure sets err and returns entt::null.
entt::entity loadGltf(World& w, entt::entity e, const std::string& path, std::string& err);
