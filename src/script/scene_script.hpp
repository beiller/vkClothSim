#pragma once
#include "components.hpp"
#include "json.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include <entt/entt.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct lua_State;
struct Engine;

// embedded Lua host: owns the VM, module cache, refs, and the per-entity hook lifecycle
class SceneScript {
public:
    SceneScript() = default;
    ~SceneScript() { shutdown(); }
    SceneScript(const SceneScript&) = delete;
    SceneScript& operator=(const SceneScript&) = delete;

    void init(Engine& w);
    void watch(entt::registry& reg);
    void shutdown();

    void activateScripts();
    void animateAll(float dt);
    void physicsAll(float dt);
    float time() const { return m_time; }

    bool loadMeshStdlib(std::string& err);
    // loads tests/t.lua as global `t` for scene test scripts; non-fatal when absent
    void loadTestStdlib();
    entt::entity callMeshGen(const std::string& gen, const std::vector<std::pair<std::string, double>>& params,
                             std::string& err);
    void buildUi(const nlohmann::json& spec);
    void button(const std::string& name);
    void destroyScript(int inst, int params, int onReady, int animate, int physics);
    // module loader shared by scene scripts, stdlib, and Lua's require
    int includeModule(const std::string& relPath, std::string& err);
    // string script vars (not bindable via UrlRef; scene.get/scene.set only)
    bool varStr(const entt::registry& reg, const std::string& url, std::string& out, std::string& err);
    void setVarStr(const entt::registry& reg, const std::string& url, const std::string& val, std::string& err);

    static SceneScript* sSelf(lua_State* L);
    Engine& w() const { return *m_w; }
    int newRef(lua_State* L, int idx);
    void dropRef(int ref);
    void reportError(const char* what);
    void addButton(const std::string& name, int ref) { m_buttons[name] = ref; }
    int nextActionSeq() { return ++m_actionSeq; }

private:
    bool callOnReady(int fnRef, int instRef);
    bool callHook(int fnRef, int instRef, float dt);
    void stepHook(float dt, int LuaScript::* hook);
    void syncVars(entt::registry& reg, bool pushToLua);
    void onScriptDestroy(entt::registry& reg, entt::entity e);
    Mesh readGeom(lua_State* L, int idx, std::string& err);

    Engine* m_w = nullptr;
    lua_State* L = nullptr;
    std::map<std::string, int> m_buttons;
    std::vector<int> m_refs;
    std::vector<int> m_errored;
    std::map<std::string, int> m_modules;
    int m_geomModule = -1;
    int m_actionSeq = 0;
    float m_time = 0.0f;
};

void luaRegisterMath(lua_State* L, SceneScript* host);
void luaRegisterComponents(lua_State* L, SceneScript* host);
void luaRegisterScene(lua_State* L, SceneScript* host);
void luaRegisterUi(lua_State* L, SceneScript* host);
void luaRegisterGraphicsTable(lua_State* L, SceneScript* host);
void luaRegisterPhysicsTable(lua_State* L, SceneScript* host);
void luaRegisterSoftTable(lua_State* L, SceneScript* host);
void luaRegisterInputTable(lua_State* L, SceneScript* host);
