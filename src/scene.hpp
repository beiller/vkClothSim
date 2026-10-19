#pragma once
#include <stdexcept>
#include <string>

struct Engine;

// a scene validation/load failure; createScene catches it and reports it (a menu-driven load
// must not kill the app)
struct SceneError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// boot: the one-time subsystem init (Jolt, the GPU sim device state, the script host + mesh
// stdlib, the destroy observers, the renderer with a neutral view)
void initEngine(Engine& w);
// build the world from a scene file: JSON data (camera, lights, materials, objects, env, set, ui)
// plus an optional .lua behavior script that runs after the JSON build (and may spawn
// programmatic content: loops, soft bodies, kinematic hierarchies, per-frame callbacks)
bool createScene(Engine& w, const std::string& path, std::string& err);
// full teardown before loading another scene: flush the deferred frees, destroy every entity
// (the destroy observers retire the per-entity resources), reset the subsystems, re-init the
// script host, clear the engine state, and re-spawn the engine-owned entities
void resetScene(Engine& w);
// replace whatever is loaded (nothing, or the current scene) with `path`: a resetScene +
// createScene. false + w.loadError set on failure (the engine state is menu-ready)
bool loadScene(Engine& w, const std::string& path);
