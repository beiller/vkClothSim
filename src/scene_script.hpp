#pragma once
#include "components.hpp"
#include "json.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include "scene.hpp"
#include "sim/sim.hpp"
#include <entt/entt.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct lua_State;
struct LuaScript;
struct Engine;

class SceneScript {
public:
    SceneScript() = default;
    ~SceneScript() { shutdown(); }
    SceneScript(const SceneScript&) = delete;
    SceneScript& operator=(const SceneScript&) = delete;

    void init(Engine& w);
    int includeModule(const std::string& relPath, std::string& err);
    bool loadMeshStdlib(std::string& err);
    entt::entity callMeshGen(const std::string& gen, const std::vector<std::pair<std::string, double>>& params,
                             std::string& err);
    void buildUi(const nlohmann::json& spec);
    void button(const std::string& name);
    void watch(entt::registry& reg);
    void shutdown();

    void activateScripts();
    void animateAll(float dt);
    void physicsAll(float dt);
    float time() const { return m_time; }
    void destroyScript(int inst, int params, int onReady, int animate, int physics);

private:
    bool callHook(int fnRef, int instRef, float dt);
    void stepHook(float dt, int LuaScript::* hook);
    bool callOnReady(int fnRef, int instRef);
    void onScriptDestroy(entt::registry& reg, entt::entity e);

private:
    Engine& w() const { return *m_w; }
    int newRef(lua_State* L, int idx);
    void dropRef(int ref);
    void reportError(const char* what);

    static SceneScript* sSelf(lua_State* L);
    static Mesh readGeom(lua_State* L, int idx, std::string& err);

    static bool readCompFields(lua_State* L, int idx, CompFields& out, std::string& err, const char* skipField = nullptr);
    static int lEntity(lua_State* L);
    static int lGltf(lua_State* L);
    static int lCompEmplace(lua_State* L);
    static int lCompReplace(lua_State* L);
    static int lComponent(lua_State* L);
    static int lCompRemove(lua_State* L);
    static int lSoftFloor(lua_State* L);
    static int lSet(lua_State* L);
    static int lGet(lua_State* L);
    static int lSetPose(lua_State* L);
    static int lGetPose(lua_State* L);
    static int lSetMorph(lua_State* L);
    static int lShapes(lua_State* L);
    static int lLinkMesh(lua_State* L);
    static int lJoints(lua_State* L);
    static int lSkeleton(lua_State* L);
    static int lTime(lua_State* L);
    static int lButton(lua_State* L);
    static int lScenes(lua_State* L);
    static int lRequestScene(lua_State* L);
    static int lLastError(lua_State* L);
    static int lUiWindow(lua_State* L);
    static int lUiTab(lua_State* L);
    static int lUiEditor(lua_State* L);
    static int lUiButton(lua_State* L);
    static int lUiText(lua_State* L);
    static int lUiSection(lua_State* L);
    static int lUiSliderF(lua_State* L);
    static int lUiSliderI(lua_State* L);
    static int lUiCheckbox(lua_State* L);
    static int lUiColor(lua_State* L);
    static int lUiDragV3(lua_State* L);
    static int lFireButton(lua_State* L);
    static int lAttachScript(lua_State* L);
    static int lDestroy(lua_State* L);
    static int lAddChild(lua_State* L);
    static int lvAdd(lua_State* L);
    static int lvSub(lua_State* L);
    static int lvScale(lua_State* L);
    static int lvLen(lua_State* L);
    static int lvNorm(lua_State* L);
    static int lvCross(lua_State* L);
    static int lvDot(lua_State* L);
    static int lqMul(lua_State* L);
    static int lqInv(lua_State* L);
    static int lqAxis(lua_State* L);
    static int lqRot(lua_State* L);
    static int lqFromTo(lua_State* L);
    static int lFind(lua_State* L);
    static int lKeyDown(lua_State* L);
    static int lRaycast(lua_State* L);

    Engine* m_w = nullptr;
    lua_State* L = nullptr;
    std::map<std::string, int> m_buttons;
    std::vector<int> m_refs;
    std::vector<int> m_errored;
    std::map<std::string, int> m_modules;
    int m_geomModule = -1;
    float m_time = 0.0f;
};
