#pragma once
#include "ecs.hpp"
#include <string>

entt::entity loadGltf(entt::registry& reg, entt::entity e, const std::string& path, std::string& err);
