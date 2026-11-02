#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

#include "assets.hpp"
#include "scene.hpp"
#include "app/engine.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <lua.hpp>
#include <vector>

namespace {

int scenePanic(lua_State* L) {
    luaL_traceback(L, L, lua_tostring(L, -1), 1);
    std::fprintf(stderr, "3dsim: lua panic:\n%s\n", lua_tostring(L, -1));
    std::abort();
    return 0;
}

void pushJson(lua_State* L, const nlohmann::json& j) {
    if (j.is_object()) {
        lua_newtable(L);
        for (const auto& [k, v] : j.items()) {
            pushJson(L, v);
            lua_setfield(L, -2, k.c_str());
        }
    } else if (j.is_array()) {
        lua_newtable(L);
        int i = 0;
        for (const auto& e : j) {
            pushJson(L, e);
            lua_rawseti(L, -2, ++i);
        }
    } else if (j.is_string())
        lua_pushstring(L, j.get<std::string>().c_str());
    else if (j.is_boolean())
        lua_pushboolean(L, j.get<bool>());
    else if (j.is_number())
        lua_pushnumber(L, j.get<double>());
    else
        lua_pushnil(L);
}

int uiTraceback(lua_State* L) {
    luaL_traceback(L, L, lua_tostring(L, -1), 0);
    return 1;
}

bool safeIncludePath(const std::string& p, std::string& err) {
    if (p.empty() || p.front() == '/') {
        err = "path must be relative to the assets dir";
        return false;
    }
    for (size_t i = 0;;) {
        const size_t j = p.find('/', i);
        const std::string c = p.substr(i, j == std::string::npos ? j : j - i);
        if (c == "..") {
            err = "path must not contain '..'";
            return false;
        }
        if (j == std::string::npos)
            return true;
        i = j + 1;
    }
}

}

SceneScript* SceneScript::sSelf(lua_State* L) {
    return (SceneScript*)lua_touserdata(L, lua_upvalueindex(1));
}

void SceneScript::init(Engine& w) {
    m_w = &w;
    L = luaL_newstate();
    lua_atpanic(L, scenePanic);
    luaL_openlibs(L);
    for (const char* lib : {"io", "os", "debug", "package"}) {
        lua_pushnil(L);
        lua_setglobal(L, lib);
    }
    lua_newtable(L);
    luaRegisterComponents(L, this);
    luaRegisterScene(L, this);
    lua_setglobal(L, "scene");

    // extend the standard math library (sqrt/sin/... stay available) with vector/quaternion helpers
    lua_getglobal(L, "math");
    luaRegisterMath(L, this);
    lua_settop(L, -1);

    lua_newtable(L);
    luaRegisterGraphicsTable(L, this);
    lua_setglobal(L, "graphics");

    lua_newtable(L);
    luaRegisterPhysicsTable(L, this);
    lua_setglobal(L, "physics");

    lua_newtable(L);
    luaRegisterSoftTable(L, this);
    lua_setglobal(L, "soft");

    lua_newtable(L);
    luaRegisterUi(L, this);
    lua_setglobal(L, "ui");

    lua_newtable(L);
    luaRegisterInputTable(L, this);
    lua_setglobal(L, "input");
    m_time = 0.0f;
}

void SceneScript::watch(entt::registry& reg) {
    reg.on_destroy<LuaScript>().connect<&SceneScript::onScriptDestroy>(this);
}

int SceneScript::includeModule(const std::string& rel, std::string& err) {
    if (!safeIncludePath(rel, err))
        return -1;
    std::string full = assetsRoot() + "/" + rel;
    if (full.size() < 4 || full.compare(full.size() - 4, 4, ".lua") != 0)
        full += ".lua";
    if (const auto it = m_modules.find(full); it != m_modules.end())
        return it->second;
    std::ifstream in(full);
    if (!in) {
        err = "cannot open '" + full + "'";
        return -1;
    }
    const std::string code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (luaL_loadbuffer(L, code.data(), code.size(), full.c_str()) != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
        err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        std::fprintf(stderr, "3dsim: %s: %s\n", full.c_str(), err.c_str());
        lua_pop(L, 1);
        return -1;
    }
    if (!lua_istable(L, -1)) {
        err = "module must return a table";
        lua_pop(L, 1);
        return -1;
    }
    const int ref = newRef(L, -1);
    lua_pop(L, 1);
    m_modules[full] = ref;
    return ref;
}

bool SceneScript::loadMeshStdlib(std::string& err) {
    const int ref = includeModule("scenes/mesh", err);
    if (ref < 0)
        return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_setglobal(L, "mesh");
    m_geomModule = ref;
    return true;
}

entt::entity SceneScript::callMeshGen(const std::string& gen, const std::vector<std::pair<std::string, double>>& params,
                                     std::string& err) {
    if (m_geomModule < 0) {
        err = "no geometry module (scenes/mesh not loaded)";
        return entt::null;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_geomModule);
    lua_getfield(L, -1, gen.c_str());
    if (!lua_isfunction(L, -1)) {
        err = "no mesh generator '" + gen + "' in geometry module";
        lua_pop(L, 2);
        return entt::null;
    }
    lua_createtable(L, (int)params.size(), 0);
    for (const auto& [k, v] : params) {
        lua_pushnumber(L, v);
        lua_setfield(L, -2, k.c_str());
    }
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        std::fprintf(stderr, "3dsim: mesh %s: %s\n", gen.c_str(), err.c_str());
        lua_pop(L, 2);
        return entt::null;
    }
    Mesh m = readGeom(L, lua_absindex(L, -1), err);
    lua_pop(L, 2);
    if (!err.empty()) {
        std::fprintf(stderr, "3dsim: mesh %s: %s\n", gen.c_str(), err.c_str());
        return entt::null;
    }
    entt::entity ge = w().reg.create();
    w().reg.emplace<Geometry>(ge, Geometry{m});
    return ge;
}

void SceneScript::buildUi(const nlohmann::json& spec) {
    std::string err;
    const int ref = includeModule("scenes/ui", err);
    if (ref < 0)
        throw SceneError(err);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_getfield(L, -1, "main");
    lua_remove(L, -2);
    pushJson(L, spec);
    lua_pushcclosure(L, uiTraceback, 0);
    lua_insert(L, 1);
    if (lua_pcall(L, 1, 0, 1) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        std::fprintf(stderr, "3dsim: buildUi error:\n%s\n", msg ? msg : "?");
        throw SceneError(msg ? msg : "unknown error");
    }
    lua_pop(L, 1);
}

Mesh SceneScript::readGeom(lua_State* L, int idx, std::string& err) {
    if (!lua_istable(L, idx)) {
        err = "expected a geometry table";
        return {};
    }
    Mesh m;
    if (!luaReadGeomField(L, idx, "pos", 3, m.pos, true, err))
        return m;
    if (!luaReadGeomField(L, idx, "nrm", 3, m.nrm, false, err))
        return m;
    if (!luaReadGeomField(L, idx, "uv", 2, m.uv, false, err))
        return m;
    if (!luaReadGeomField(L, idx, "idx", 1, m.indices, true, err))
        return m;
    std::vector<float> jf, wf;
    if (!luaReadGeomField(L, idx, "joints", 4, jf, false, err))
        return m;
    if (!luaReadGeomField(L, idx, "weights", 4, wf, false, err))
        return m;
    const int n = m.vertexCount();
    if (m.nrm.empty())
        m.nrm.assign((size_t)3 * n, 0.0f);
    if (m.uv.empty())
        m.uv.assign((size_t)2 * n, 0.0f);
    for (uint32_t i : m.indices)
        if (i >= (uint32_t)n) {
            err = "geometry: index out of range";
            return {};
        }
    if (!jf.empty() || !wf.empty()) {
        if (jf.size() != (size_t)4 * n || wf.size() != (size_t)4 * n) {
            err = "geometry: 'joints' and 'weights' must each have 4 * vertexCount entries";
            return {};
        }
        for (float v : jf)
            m.joints.push_back((uint32_t)v);
        m.weights = std::move(wf);
    }
    if (!luaReadShapesTable(L, idx, n, m.shapes, err))
        return {};
    return m;
}

void SceneScript::button(const std::string& name) {
    if (name == "demo-window") { w().showDemo = true; return; }
    if (!L)
        return;
    const auto it = m_buttons.find(name);
    if (it == m_buttons.end()) {
        std::fprintf(stderr, "3dsim: script has no handler for button '%s'\n", name.c_str());
        return;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        reportError(("button callback '" + name + "'").c_str());
        dropRef(it->second);
        m_buttons.erase(it);
    }
}

bool SceneScript::callOnReady(int fnRef, int instRef) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef);
    lua_rawgeti(L, LUA_REGISTRYINDEX, instRef);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        if (std::ranges::find(m_errored, fnRef) == m_errored.end()) {
            m_errored.push_back(fnRef);
            reportError("_on_ready");
        } else {
            lua_pop(L, 1);
        }
        return false;
    }
    return true;
}

bool SceneScript::callHook(int fnRef, int instRef, float dt) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef);
    lua_rawgeti(L, LUA_REGISTRYINDEX, instRef);
    lua_pushnumber(L, dt);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        if (std::ranges::find(m_errored, fnRef) == m_errored.end()) {
            m_errored.push_back(fnRef);
            reportError("script hook");
        } else {
            lua_pop(L, 1);
        }
        return false;
    }
    return true;
}

void SceneScript::destroyScript(int inst, int params, int onReady, int animate, int physics) {
    for (int ref : {inst, params, onReady, animate, physics})
        if (ref > 0)
            dropRef(ref);
}

// parse "name.LuaScript.varpath" (last marker occurrence; entity names may contain dots)
static bool parseVarUrl(const std::string& url, std::string& name, std::string& path) {
    const size_t m = url.rfind(".LuaScript.");
    if (m == std::string::npos || m == 0 || m + 11 >= url.size())
        return false;
    name = url.substr(0, m);
    path = url.substr(m + 11);
    return true;
}

static void setDotted(lua_State* L, int baseIdx, const std::string& key, int valAbs);

static const LuaScript* findVarScript(const entt::registry& reg, const std::string& name, std::string& err) {
    entt::entity ent = entt::null;
    int found = 0;
    for (auto [e, n] : reg.view<Name>().each()) {
        if (n.id == name) {
            ent = e;
            ++found;
        }
        if (found > 1) {
            err = "entity name '" + name + "' is not unique";
            return nullptr;
        }
    }
    if (found == 0) {
        err = "unknown entity '" + name + "'";
        return nullptr;
    }
    auto* ls = reg.try_get<LuaScript>(ent);
    if (!ls || ls->inst < 0) {
        err = "entity '" + name + "' has no script";
        return nullptr;
    }
    return ls;
}

bool SceneScript::varStr(const entt::registry& reg, const std::string& url, std::string& out, std::string& err) {
    std::string name, path;
    if (!parseVarUrl(url, name, path)) {
        err = "expected 'name.LuaScript.var'";
        return false;
    }
    auto* ls = findVarScript(reg, name, err);
    if (!ls)
        return false;
    auto it = ls->varsS.find(path);
    if (it == ls->varsS.end()) {
        err = "no var '" + path + "' in script of '" + name + "'";
        return false;
    }
    out = it->second;
    return true;
}

void SceneScript::setVarStr(const entt::registry& reg, const std::string& url, const std::string& val, std::string& err) {
    std::string name, path;
    if (!parseVarUrl(url, name, path)) {
        err = "expected 'name.LuaScript.var'";
        return;
    }
    auto* ls = const_cast<LuaScript*>(findVarScript(reg, name, err));  // storage is mutable
    if (!ls)
        return;
    // write the lua table now (a mirror-only write would be clobbered by the next pull)
    if (L) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ls->inst);
        lua_getfield(L, -1, "vars");
        if (lua_istable(L, -1)) {
            lua_pushstring(L, val.c_str());
            setDotted(L, lua_gettop(L) - 1, path, lua_gettop(L));
            lua_pop(L, 1);  // setDotted copied the value; drop the original
        }
        lua_pop(L, 2);
    }
    ls->varsS[path] = val;
}

void SceneScript::onScriptDestroy(entt::registry& reg, entt::entity e) {
    if (const auto* ls = reg.try_get<LuaScript>(e))
        destroyScript(ls->inst, ls->params, ls->onReady, ls->animate, ls->physics);
}

// script module contract: a .lua file attached via scene.attach_script must return a table (the module).
// The engine builds a per-attach instance table and calls the module's hooks with it as `self`.
/// script-instance | per-attach instance table passed as self to every hook; fields: entity (the attached entity), params (the table passed to scene.attach_script, when given); its __index metatable is the module, so module functions act as methods and inst fields hold per-instance state
/// script-hook _on_ready(inst) | called once when the attached module has loaded
/// script-hook _animate(inst, dt) | called every frame with the frame delta time in seconds
/// script-hook _physics(inst, dt) | called every fixed physics step (120 Hz) before the Jolt step; the place to write kinematic poses
void SceneScript::activateScripts() {
    if (!L)
        return;
    for (;;) {
        std::vector<entt::entity> pending;
        for (auto [e, ls] : w().reg.view<LuaScript>().each())
            if (!ls.ready)
                pending.push_back(e);
        if (pending.empty())
            break;
        for (auto e : pending) {
            if (!w().reg.valid(e))
                continue;
            std::string file;
            int paramsRef = -1;
            {
                const auto& ls = w().reg.get<LuaScript>(e);
                file = ls.file;
                paramsRef = ls.params;
            }
            std::string err;
            const int mod = includeModule(file, err);
            if (mod < 0) {
                std::fprintf(stderr, "3dsim: script '%s': %s\n", file.c_str(), err.c_str());
                if (auto* ls = w().reg.try_get<LuaScript>(e))
                    ls->ready = true;
                continue;
            }
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod);
            lua_newtable(L);
            luaPushEnt(L, e);
            lua_setfield(L, -2, "entity");
            if (paramsRef > 0) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, paramsRef);
                lua_setfield(L, -2, "params");
            }
            lua_newtable(L);
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod);
            lua_setfield(L, -2, "__index");
            lua_setmetatable(L, -2);
            const int instRef = newRef(L, -1);
            int onReadyRef = -1, animateRef = -1, physicsRef = -1;
            const auto grab = [this, mod](const char* name, int& out) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, mod);
                lua_getfield(L, -1, name);
                if (lua_isfunction(L, -1))
                    out = newRef(L, -1);
                lua_pop(L, 2);
            };
            grab("_on_ready", onReadyRef);
            grab("_animate", animateRef);
            grab("_physics", physicsRef);
            lua_pop(L, 2);
            auto* ls = w().reg.try_get<LuaScript>(e);
            if (!ls) {
                dropRef(instRef);
                dropRef(onReadyRef);
                dropRef(animateRef);
                dropRef(physicsRef);
                continue;
            }
            // inst is set before _on_ready so "name.LuaScript.var" binds resolve while it runs;
            // the pull fills the vars mirror from (module-level) inst.vars defaults first.
            ls->inst = instRef;
            syncVars(w().reg, false);
            const bool ok = (onReadyRef <= 0) || callOnReady(onReadyRef, instRef);
            if (!ok) {
                dropRef(instRef);
                dropRef(paramsRef);
                dropRef(onReadyRef);
                dropRef(animateRef);
                dropRef(physicsRef);
                ls->inst = ls->params = ls->onReady = ls->animate = ls->physics = -1;
            } else {
                syncVars(w().reg, false);  // pick up vars (re)assigned inside _on_ready
                ls->onReady = onReadyRef;
                ls->animate = animateRef;
                ls->physics = physicsRef;
            }
            ls->ready = true;
        }
    }
}

/// script-instance | vars: a module-level `M.vars` table provides defaults (visible via __index); each instance's `self.vars` is mirrored into typed C++ maps every frame — pulled after every hook pass, pushed back before — and is bindable through `name.LuaScript.var` URLs; numbers map to float, booleans and strings stay typed, 3/4-number tables become vec3/quat slots, and dotted sub-paths address members (`hello.LuaScript.pos.x`)
// pull one lua value (at idx) into the mirror under dotted `key`. 3/4-number tables
// become contiguous vec slots; other tables recurse with dotted keys. Stack-balanced.
static void pullValue(lua_State* L, int idx, const std::string& key, LuaScript& ls) {
    if (lua_isnumber(L, idx)) {
        ls.varsF[key] = (float)lua_tonumber(L, idx);
        return;
    }
    if (lua_isboolean(L, idx)) {
        ls.varsB[key] = lua_toboolean(L, idx);
        return;
    }
    if (lua_isstring(L, idx)) {
        ls.varsS[key] = lua_tostring(L, idx);
        return;
    }
    if (!lua_istable(L, idx))
        return;

    float vals[4]{};
    int n = 0, nums = 0;
    const int tAbs = lua_absindex(L, idx);
    lua_pushnil(L);
    while (lua_next(L, tAbs) != 0) {
        ++n;
        if (lua_isnumber(L, -1) && nums < 4)
            vals[nums++] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    if (n == 3 && nums == 3) {
        ls.varsV3[key] = V3(vals[0], vals[1], vals[2]);
        return;
    }
    if (n == 4 && nums == 4) {
        ls.varsQ[key] = Quat(vals[0], vals[1], vals[2], vals[3]);
        return;
    }

    const std::string prefix = key.empty() ? "" : key + ".";
    lua_pushnil(L);
    while (lua_next(L, tAbs) != 0) {
        if (const char* k = lua_tostring(L, -2))
            pullValue(L, -1, prefix + k, ls);
        lua_pop(L, 1);
    }
}

// store the value at absolute index valAbs into the table at absolute index baseIdx under
// dotted `key`, creating intermediate tables. Stack-balanced (pops its own intermediates).
static void setDotted(lua_State* L, int baseIdx, const std::string& key, int valAbs) {
    int cur = baseIdx;
    int pushed = 0;
    size_t start = 0;
    for (;;) {
        const size_t dot = key.find('.', start);
        const bool last = dot == std::string::npos;
        const std::string seg = key.substr(start, last ? std::string::npos : dot - start);
        if (last) {
            lua_pushstring(L, seg.c_str());  // settable wants key on top, value below
            lua_pushvalue(L, valAbs);
            lua_settable(L, cur);
            break;
        }
        lua_getfield(L, cur, seg.c_str());
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushstring(L, seg.c_str());
            lua_settable(L, cur);
            lua_getfield(L, cur, seg.c_str());
        }
        ++pushed;
        cur = -1;
        start = dot + 1;
    }
    for (int i = 0; i < pushed; ++i)
        lua_pop(L, 1);
}

// sync the vars mirror with each script's inst.vars table: pushToLua copies mirror -> table
// (so hooks see widget writes), otherwise table -> mirror (so widgets/scene.get see hook
// writes). The pull never erases mirror entries, keeping bound UrlRef slots stable.
void SceneScript::syncVars(entt::registry& reg, bool pushToLua) {
    if (!L)
        return;
    for (auto [e, ls] : reg.view<LuaScript>().each()) {
        if (ls.inst <= 0)
            continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ls.inst);
        lua_getfield(L, -1, "vars");
        const bool hasTable = lua_istable(L, -1);
        if (hasTable) {
            const int varsAbs = lua_gettop(L);
            if (pushToLua) {
                for (const auto& [k, v] : ls.varsF) {
                    lua_pushnumber(L, v);
                    setDotted(L, varsAbs, k, lua_gettop(L));
                    lua_pop(L, 1);  // setDotted copied the value; drop the original
                }
                for (const auto& [k, v] : ls.varsB) {
                    lua_pushboolean(L, v);
                    setDotted(L, varsAbs, k, lua_gettop(L));
                    lua_pop(L, 1);
                }
                for (const auto& [k, v] : ls.varsS) {
                    lua_pushstring(L, v.c_str());
                    setDotted(L, varsAbs, k, lua_gettop(L));
                    lua_pop(L, 1);
                }
                for (const auto& [k, v] : ls.varsV3) {
                    lua_createtable(L, 3, 0);
                    for (int i = 0; i < 3; ++i) {
                        lua_pushnumber(L, v[i]);
                        lua_rawseti(L, -2, i + 1);
                    }
                    setDotted(L, varsAbs, k, lua_gettop(L));
                    lua_pop(L, 1);
                }
                for (const auto& [k, v] : ls.varsQ) {
                    lua_createtable(L, 4, 0);
                    for (int i = 0; i < 4; ++i) {
                        lua_pushnumber(L, v[i]);
                        lua_rawseti(L, -2, i + 1);
                    }
                    setDotted(L, varsAbs, k, lua_gettop(L));
                    lua_pop(L, 1);
                }
            } else {
                lua_pushnil(L);
                while (lua_next(L, varsAbs) != 0) {
                    if (const char* k = lua_tostring(L, -2))
                        pullValue(L, -1, k, ls);
                    lua_pop(L, 1);
                }
            }
        }
        lua_pop(L, 2);
    }
}

void SceneScript::stepHook(float dt, int LuaScript::* hook) {
    struct Call {
        entt::entity e;
        int fn;
        int inst;
    };
    std::vector<Call> calls;
    for (auto [e, ls] : w().reg.view<LuaScript>().each())
        if (ls.ready && ls.*hook > 0)
            calls.push_back({e, ls.*hook, ls.inst});
    for (auto& c : calls) {
        if (callHook(c.fn, c.inst, dt))
            continue;
        if (auto* ls = w().reg.try_get<LuaScript>(c.e)) {
            dropRef(ls->*hook);
            ls->*hook = -1;
        }
    }
}

void SceneScript::animateAll(float dt) {
    if (!L)
        return;
    m_time += dt;
    syncVars(w().reg, true);
    stepHook(dt, &LuaScript::animate);
    syncVars(w().reg, false);
}

void SceneScript::physicsAll(float dt) {
    if (!L)
        return;
    syncVars(w().reg, true);
    stepHook(dt, &LuaScript::physics);
    syncVars(w().reg, false);
}

void SceneScript::shutdown() {
    if (!L)
        return;
    for (int ref : m_refs)
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    m_refs.clear();
    m_buttons.clear();
    m_errored.clear();
    m_modules.clear();
    m_geomModule = -1;
    lua_close(L);
    L = nullptr;
    m_w = nullptr;
}

int SceneScript::newRef(lua_State* L, int idx) {
    lua_pushvalue(L, idx);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    m_refs.push_back(ref);
    return ref;
}

void SceneScript::dropRef(int ref) {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    m_refs.erase(std::remove(m_refs.begin(), m_refs.end(), ref), m_refs.end());
}

void SceneScript::reportError(const char* what) {
    luaL_traceback(L, L, lua_tostring(L, -1), 1);
    const char* tb = lua_tostring(L, -1);
    std::fprintf(stderr, "3dsim: script %s: %s\n", what, tb ? tb : "?");
    lua_pop(L, 2);
}
