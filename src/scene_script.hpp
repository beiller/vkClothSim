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

// Embedded Lua 5.4 host for scene behavior scripts; scripts see only the `scene` table
// (io/os/debug/package are removed); errors go to stderr and never crash the engine
class SceneScript {
public:
    SceneScript() = default;
    ~SceneScript() { shutdown(); }
    SceneScript(const SceneScript&) = delete;
    SceneScript& operator=(const SceneScript&) = delete;

    void init(Engine& w);
    // restricted module loader: run a .lua file under the assets dir once and cache its
    // (table) result; registry ref, or -1 on error
    int includeModule(const std::string& relPath, std::string& err);
    // load the geometry stdlib ("scenes/mesh") as the global `mesh` and the geometry module
    // used by callMeshGen (declarative meshes + VR controller grips); false on error
    bool loadMeshStdlib(std::string& err);
    // call geometry-module generator gen(params) and store the geometry table it returns as a
    // Geometry component on a new entity; that entity, or entt::null on error
    entt::entity callMeshGen(const std::string& gen, const std::vector<std::pair<std::string, double>>& params,
                             std::string& err);
    // run the shared ui module (scenes/ui) build(parent, spec) for a scene json ui.widgets spec;
    // a module or spec error throws SceneError (a scene load failure)
    void buildUi(const nlohmann::json& spec);
    void button(const std::string& name);
    // register the on_destroy<LuaScript> hook (once, at boot: entt sinks stack, and a
    // re-connection would double-unref against the then-current state)
    void watch(entt::registry& reg);
    void shutdown();

    // ---- per-entity behavior scripts (LuaScript component) ----
    // activate every not-ready script entity (load module, build instance, call _on_ready);
    // runs to fixpoint so a script that spawns a child script entity activates it too
    void activateScripts();
    // call _animate(self, dt) on every ready script entity (per render frame)
    void animateAll(float dt);
    // call _physics(self, dt) on every ready script entity (per physics step)
    void physicsAll(float dt);
    float time() const { return m_time; }
    // unref a script entity's Lua refs (invoked from the on_destroy<LuaScript> hook; takes the
    // refs directly so it stays safe while the entity's components are being torn down)
    void destroyScript(int inst, int params, int onReady, int animate, int physics);

private:
    // call a hook fn(self, dt) via its registry ref; reports an error once; false on failure
    bool callHook(int fnRef, int instRef, float dt);
    // run one hook field (_animate or _physics) over every ready script; a hook that errors is
    // unrefed and disabled
    void stepHook(float dt, int LuaScript::* hook);
    // call a hook fn(self) (no dt); reports an error once; false on failure
    bool callOnReady(int fnRef, int instRef);
    // on_destroy<LuaScript> handler: unref the destroyed entity's script refs
    void onScriptDestroy(entt::registry& reg, entt::entity e);

private:
    Engine& w() const { return *m_w; }
    int newRef(lua_State* L, int idx);
    void dropRef(int ref);
    // the error object is on top of the stack
    void reportError(const char* what);

    static SceneScript* sSelf(lua_State* L);
    // geometry table at idx: {pos, nrm?, uv?, idx} -> Mesh; sets err on failure
    static Mesh readGeom(lua_State* L, int idx, std::string& err);

    // {field = number | boolean | string | {numbers}} at idx -> CompFields (struct-form
    // vocabulary); sets err
    static bool readCompFields(lua_State* L, int idx, CompFields& out, std::string& err);
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
    static int lJoints(lua_State* L);
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
    std::map<std::string, int> m_buttons; // action name -> registry ref
    std::vector<int> m_refs;              // every registry ref (unrefed at shutdown)
    std::vector<int> m_errored;           // anim refs that already reported an error
    std::map<std::string, int> m_modules; // resolved module path -> registry ref (load-once cache)
    int m_geomModule = -1;                // registry ref of the geometry module (LUA_NOREF if unset)
    float m_time = 0.0f;
};
