#pragma once
#include <entt/entt.hpp>
#include <stdexcept>
#include <string>

struct Engine;

struct SceneError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void initEngine(Engine& w);
bool createScene(Engine& w, const std::string& path, std::string& err);
void resetScene(Engine& w);
bool loadScene(Engine& w, const std::string& path);
void spawnEditorUi(Engine& w, entt::entity tab);
