#pragma once
#include <string>

struct World;

// build the world from a scene file: JSON data (camera, lights, materials, objects, env, set, ui)
// plus an optional .lua behavior script that runs after the JSON build (and may spawn
// programmatic content: loops, soft bodies, kinematic hierarchies, per-frame callbacks)
void createSceneWorld(World& w, const std::string& path);
