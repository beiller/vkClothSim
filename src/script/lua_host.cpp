#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

#include "assets.hpp"
#include "scene.hpp"
#include "engine.hpp"
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
    luaRegisterMath(L, this);
    luaRegisterComponents(L, this);
    luaRegisterScene(L, this);
    luaRegisterUi(L, this);
    lua_setglobal(L, "scene");

    lua_newtable(L);
    luaRegisterRenderTable(L, this);
    lua_setglobal(L, "render");

    lua_newtable(L);
    luaRegisterPhysicsTable(L, this);
    lua_setglobal(L, "physics");

    lua_newtable(L);
    luaRegisterSoftTable(L, this);
    lua_setglobal(L, "soft");
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

void SceneScript::onScriptDestroy(entt::registry& reg, entt::entity e) {
    if (const auto* ls = reg.try_get<LuaScript>(e))
        destroyScript(ls->inst, ls->params, ls->onReady, ls->animate, ls->physics);
}

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
            const bool ok = (onReadyRef <= 0) || callOnReady(onReadyRef, instRef);
            auto* ls = w().reg.try_get<LuaScript>(e);
            if (!ls) {
                dropRef(instRef);
                dropRef(onReadyRef);
                dropRef(animateRef);
                dropRef(physicsRef);
                continue;
            }
            if (!ok) {
                dropRef(instRef);
                dropRef(paramsRef);
                dropRef(onReadyRef);
                dropRef(animateRef);
                dropRef(physicsRef);
                ls->inst = ls->params = ls->onReady = ls->animate = ls->physics = -1;
            } else {
                ls->inst = instRef;
                ls->onReady = onReadyRef;
                ls->animate = animateRef;
                ls->physics = physicsRef;
            }
            ls->ready = true;
        }
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
    stepHook(dt, &LuaScript::animate);
}

void SceneScript::physicsAll(float dt) {
    if (!L)
        return;
    stepHook(dt, &LuaScript::physics);
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
