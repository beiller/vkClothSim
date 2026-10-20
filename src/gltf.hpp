#pragma once
#include "engine.hpp"
#include <string>

entt::entity loadGltf(Engine& w, entt::entity e, const std::string& path, std::string& err);
