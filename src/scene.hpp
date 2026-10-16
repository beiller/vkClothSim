#pragma once
#include <string>

struct Engine;

// build the world from a scene file: JSON data (camera, lights, materials, objects, env, set, ui)
// plus an optional .lua behavior script that runs after the JSON build (and may spawn
// programmatic content: loops, soft bodies, kinematic hierarchies, per-frame callbacks)
void createScene(Engine& w, const std::string& path);
