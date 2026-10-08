#pragma once
#include <string>

struct World;

// build a World from a JSON scene file; exits on parse/asset errors
void createSceneFileWorld(World& w, const std::string& path);
