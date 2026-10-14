#include "scene_script.hpp"

#include "assets.hpp"
#include "systems.hpp"
#include "url.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <lua.hpp>
#include <vector>

namespace {

// last-resort error dump when an unprotected Lua error escapes into C
int scenePanic(lua_State* L) {
    luaL_traceback(L, L, lua_tostring(L, -1), 1);
    std::fprintf(stderr, "3dsim: lua panic:\n%s\n", lua_tostring(L, -1));
    std::abort();
    return 0;
}

// relPath must be relative and stay under the assets root (no leading '/', no '..' component)
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

// sequential array of numbers in the table at absidx -> out
template <class T> bool readNumArray(lua_State* L, int absidx, std::vector<T>& out, std::string& err) {
    if (!lua_istable(L, absidx)) {
        err = "must be an array of numbers";
        return false;
    }
    for (int i = 1;; ++i) {
        lua_geti(L, absidx, i);
        if (lua_isnoneornil(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        if (!lua_isnumber(L, -1)) {
            err = "must be an array of numbers";
            lua_pop(L, 1);
            return false;
        }
        out.push_back((T)lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    return true;
}

// named geometry field at tidx -> out (1-based array of numbers); nil is ok when !required
template <class T>
bool readGeomField(lua_State* L, int tidx, const char* name, size_t per, std::vector<T>& out, bool required,
                   std::string& err) {
    lua_getfield(L, tidx, name);
    if (lua_isnoneornil(L, -1)) {
        lua_pop(L, 1);
        if (required) {
            err = std::string("geometry: missing '") + name + "'";
            return false;
        }
        return true;
    }
    if (!readNumArray(L, lua_absindex(L, -1), out, err)) {
        err = std::string("geometry: '") + name + "' " + err;
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);
    if (out.size() % per != 0) {
        err = std::string("geometry: '") + name + "' length is not a multiple of " + std::to_string(per);
        out.clear();
        return false;
    }
    return true;
}

entt::entity readEnt(lua_State* L, int idx) {
    return static_cast<entt::entity>((uint32_t)luaL_checkinteger(L, idx));
}

void pushEnt(lua_State* L, entt::entity e) {
    lua_pushinteger(L, (lua_Integer)entt::to_integral(e));
}

float readNum(lua_State* L, int tidx, int i, float def) {
    lua_geti(L, tidx, i);
    const float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

// {x, y, z} or nil -> default
V3 readV3(lua_State* L, int idx, const V3& def = {0, 0, 0}) {
    if (!lua_istable(L, idx))
        return def;
    return {readNum(L, idx, 1, def.x), readNum(L, idx, 2, def.y), readNum(L, idx, 3, def.z)};
}

// {x, y, z, w} or nil -> identity
V4 readQuat(lua_State* L, int idx) {
    if (!lua_istable(L, idx))
        return {0, 0, 0, 1};
    return {readNum(L, idx, 1, 0), readNum(L, idx, 2, 0), readNum(L, idx, 3, 0), readNum(L, idx, 4, 1)};
}

void pushV3(lua_State* L, V3 v) {
    lua_createtable(L, 3, 0);
    lua_pushnumber(L, v.x);
    lua_seti(L, -2, 1);
    lua_pushnumber(L, v.y);
    lua_seti(L, -2, 2);
    lua_pushnumber(L, v.z);
    lua_seti(L, -2, 3);
}

void pushV4(lua_State* L, V4 q) {
    lua_createtable(L, 4, 0);
    lua_pushnumber(L, q.x);
    lua_seti(L, -2, 1);
    lua_pushnumber(L, q.y);
    lua_seti(L, -2, 2);
    lua_pushnumber(L, q.z);
    lua_seti(L, -2, 3);
    lua_pushnumber(L, q.w);
    lua_seti(L, -2, 4);
}

entt::entity optEnt(lua_State* L, int tidx, const char* key) {
    lua_getfield(L, tidx, key);
    const entt::entity e = lua_isnumber(L, -1) ? readEnt(L, -1) : entt::null;
    lua_pop(L, 1);
    return e;
}

bool optBool(lua_State* L, int tidx, const char* key) {
    lua_getfield(L, tidx, key);
    const bool b = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return b;
}

} // namespace

SceneScript* SceneScript::sSelf(lua_State* L) {
    return (SceneScript*)lua_touserdata(L, lua_upvalueindex(1));
}

void SceneScript::init(World& w) {
    m_w = &w;
    L = luaL_newstate();
    lua_atpanic(L, scenePanic);
    luaL_openlibs(L);
    // no filesystem, clock, or introspection from scene scripts
    for (const char* lib : {"io", "os", "debug", "package"}) {
        lua_pushnil(L);
        lua_setglobal(L, lib);
    }
    const auto reg = [this](const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, fn, 1); // pops the upvalue: stack is [table, closure]
        lua_setfield(L, -2, name);
    };

    lua_newtable(L);                     // scene: the ECS graph, components, scripts, time, UI, and the math kit
    reg("entity", lEntity);              // entity(name?) -> new empty entity
    reg("emplace", lCompEmplace);        // emplace(entity, "Type", {field = ...}) - create; error if present
    reg("replace", lCompReplace);        // replace(entity, "Type", {field = ...}) - create or assign
    reg("component", lComponent);        // component(entity, "Type") -> {field = ...} | nil
    reg("remove", lCompRemove);          // remove(entity, "Type") - pure-data components
    reg("set", lSet);                    // set(url | entity, field, value | {values})
    reg("get", lGet);                    // get(url | entity, field) -> number | {x, y, z}
    reg("time", lTime);                  // time() -> seconds since scene start
    reg("button", lButton);              // button(name, fn())
    reg("attach_script", lAttachScript); // attach_script(entity, file, params?)
    reg("destroy", lDestroy);            // destroy(entity)
    reg("add_child", lAddChild);         // add_child(parent, child)
    reg("remove_child", lRemoveChild);   // remove_child(child)
    reg("get_parent", lGetParent);       // get_parent(child) -> entity | nil
    reg("get_children", lGetChildren);   // get_children(entity) -> {entity, ...}
    reg("vadd", lvAdd);
    reg("vsub", lvSub);
    reg("vscale", lvScale);
    reg("vlen", lvLen);
    reg("vnorm", lvNorm);
    reg("vcross", lvCross);
    reg("vdot", lvDot);
    reg("qmul", lqMul);
    reg("qinv", lqInv);
    reg("qaxis", lqAxis);
    reg("qrot", lqRot);
    reg("qfromto", lqFromTo);
    reg("find", lFind);        // find(name) -> handle | nil
    reg("is_vr", lIsVr);       // is_vr() -> bool
    reg("key_down", lKeyDown); // key_down(entity, code) -> bool
    lua_setglobal(L, "scene");

    lua_newtable(L);            // render: mesh assets, materials, and visuals
    reg("geometry", lGeometry); // geometry(geomTable) -> shared mesh id
    reg("texture", lTexture);   // texture(path) -> shared texture entity (one per path)
    reg("material", lMaterial); // material(name, color, metallic, roughness, texEntity?, emissive?)
    reg("mesh", lMesh);         // mesh(entity, geom, mat, pos?, quat?, {parent?, noShadow?})
    lua_setglobal(L, "render");

    lua_newtable(L);                     // physics: shapes, rigid + kinematic bodies, raycasts, joints
    reg("capsule", lCapsuleShape);       // capsule(halfLen, radius) -> shared shape entity
    reg("box", lBoxShape);               // box(hx, hy, hz) -> shared shape entity
    reg("body", lBody);                  // body(entity, colliders, {static = 1}?) -> rigid body + collider children
    reg("kinematic", lKinematic);        // kinematic(entity, colliders) -> kinematic body + collider children
    reg("raycast", lRaycast);            // raycast(origin, dir, maxDist, excludes?) -> {hit,pos,dist,entity} | nil
    reg("create_joint", lCreateJoint);   // create_joint(entA, entB, type, grabPos) -> joint id
    reg("destroy_joint", lDestroyJoint); // destroy_joint(id)
    reg("is_dynamic", lIsDynamic);       // is_dynamic(entity) -> bool
    lua_setglobal(L, "physics");

    lua_newtable(L);          // soft: GPU soft bodies (a mesh + its constraints)
    reg("body", lSoftBody);   // body(entity, geom, cons, mat, mass?) - entity's Transform = rest pose
    reg("floor", lSoftFloor); // floor(y) - the sim's floor plane height
    lua_setglobal(L, "soft");
    // unref a script entity's Lua refs when it is destroyed
    w.reg.on_destroy<LuaScript>().connect<&SceneScript::onScriptDestroy>(this);
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

int SceneScript::callMeshGen(const std::string& gen, const std::vector<std::pair<std::string, double>>& params,
                             std::string& err) {
    if (m_geomModule < 0) {
        err = "no geometry module (scenes/mesh not loaded)";
        return -1;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_geomModule);
    lua_getfield(L, -1, gen.c_str());
    if (!lua_isfunction(L, -1)) {
        err = "no mesh generator '" + gen + "' in geometry module";
        lua_pop(L, 2);
        return -1;
    }
    lua_createtable(L, (int)params.size(), 0);
    for (const auto& [k, v] : params) {
        lua_pushnumber(L, v);
        lua_setfield(L, -2, k.c_str());
    }
    // stack: [module, fn, params]; this liblua5.4 takes the function at top-nargs (fn under its
    // args, as in the anim/frame callbacks), so call it in place
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        std::fprintf(stderr, "3dsim: mesh %s: %s\n", gen.c_str(), err.c_str());
        lua_pop(L, 2); // module, error
        return -1;
    }
    Mesh m = readGeom(L, lua_absindex(L, -1), err);
    lua_pop(L, 2); // module, geom
    if (!err.empty()) {
        std::fprintf(stderr, "3dsim: mesh %s: %s\n", gen.c_str(), err.c_str());
        return -1;
    }
    return w().renderer.addMesh(m).geom;
}

Mesh SceneScript::readGeom(lua_State* L, int idx, std::string& err) {
    if (!lua_istable(L, idx)) {
        err = "expected a geometry table";
        return {};
    }
    Mesh m;
    if (!readGeomField(L, idx, "pos", 3, m.pos, true, err))
        return m;
    if (!readGeomField(L, idx, "nrm", 3, m.nrm, false, err))
        return m;
    if (!readGeomField(L, idx, "uv", 2, m.uv, false, err))
        return m;
    if (!readGeomField(L, idx, "idx", 1, m.indices, true, err))
        return m;
    const int n = m.vertexCount();
    if (m.nrm.empty())
        m.nrm.assign((size_t)3 * n, 0.0f);
    if (m.uv.empty())
        m.uv.assign((size_t)2 * n, 0.0f);
    // Lua indices are 1-based; Vulkan wants 0-based
    for (auto& i : m.indices) {
        if (i < 1 || i > (uint32_t)n) {
            err = "geometry: index out of range";
            return {};
        }
        i -= 1;
    }
    return m;
}

// geometry table at stack slot 1 (errors via luaL_error)
Mesh SceneScript::checkGeom(lua_State* L, const char* what) {
    std::string err;
    Mesh m = readGeom(L, lua_absindex(L, 1), err);
    if (!err.empty())
        luaL_error(L, "%s: %s", what, err.c_str());
    return m;
}

std::vector<sim::Constraint> SceneScript::readCons(lua_State* L, int idx, int nVerts, std::string& err) {
    std::vector<sim::Constraint> out;
    std::vector<float> flat;
    if (!readNumArray(L, idx, flat, err)) {
        err = "constraints " + err;
        return out;
    }
    if (flat.size() % 4 != 0) {
        err = "constraints length is not a multiple of 4 (a, b, rest, k)";
        return out;
    }
    out.reserve(flat.size() / 4);
    for (size_t i = 0; i < flat.size(); i += 4) {
        const int a = (int)flat[i] - 1, b = (int)flat[i + 1] - 1;
        if (a < 0 || a >= nVerts || b < 0 || b >= nVerts) {
            err = "constraint index out of range";
            return {};
        }
        out.push_back({a, b, flat[i + 2], flat[i + 3]});
    }
    return out;
}

bool SceneScript::hasButton(const std::string& name) const {
    return L != nullptr && m_buttons.contains(name);
}

void SceneScript::button(const std::string& name) {
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
    lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef);   // [fn]
    lua_rawgeti(L, LUA_REGISTRYINDEX, instRef); // [fn, self]
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
    lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef);   // [fn]
    lua_rawgeti(L, LUA_REGISTRYINDEX, instRef); // [fn, self]
    lua_pushnumber(L, dt);                      // [fn, self, dt]
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
            // snapshot the component; do not hold a reference across _on_ready (which may spawn
            // new script entities and reallocate the storage, invalidating the reference)
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
                    ls->ready = true; // inert; do not retry every frame
                continue;
            }
            // instance: setmetatable({entity = e, params = params?}, {__index = module})
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod); // [module]
            lua_newtable(L);                        // [module, inst]
            pushEnt(L, e);                          // [module, inst, e]
            lua_setfield(L, -2, "entity");          // [module, inst]
            if (paramsRef > 0) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, paramsRef); // [module, inst, params]
                lua_setfield(L, -2, "params");                // [module, inst]
            }
            lua_newtable(L);                        // [module, inst, mt]
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod); // [module, inst, mt, module]
            lua_setfield(L, -2, "__index");         // [module, inst, mt]
            lua_setmetatable(L, -2);                // [module, inst]
            const int instRef = newRef(L, -1);      // [module, inst]
            int onReadyRef = -1, animateRef = -1, physicsRef = -1;
            const auto grab = [this, mod](const char* name, int& out) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, mod); // [.., module]
                lua_getfield(L, -1, name);              // [.., module, fn|nil]
                if (lua_isfunction(L, -1))
                    out = newRef(L, -1);
                lua_pop(L, 2); // [..]
            };
            grab("_on_ready", onReadyRef);
            grab("_animate", animateRef);
            grab("_physics", physicsRef);
            lua_pop(L, 2); // module, inst
            const bool ok = (onReadyRef <= 0) || callOnReady(onReadyRef, instRef);
            auto* ls = w().reg.try_get<LuaScript>(e);
            if (!ls) {
                // the entity was destroyed during _on_ready; its on_destroy hook already unrefed
                // the params, so drop only the refs created here
                dropRef(instRef);
                dropRef(onReadyRef);
                dropRef(animateRef);
                dropRef(physicsRef);
                continue;
            }
            if (!ok) {
                // setup failed: tear down the hooks, leave the entity inert
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
            dropRef(ls->*hook); // the hook that just errored is dropped and disabled
            ls->*hook = -1;
        }
    }
}

void SceneScript::animateAll(float dt) {
    if (!L)
        return;
    m_time += dt; // scene time (render-rate), read by scene.time()
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
    lua_pop(L, 2); // traceback + error object
}

int SceneScript::lTexture(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const char* path = luaL_checkstring(L, 1);
    const entt::entity e = textureEntity(w, path);
    if (e == entt::null)
        return luaL_error(L, "render.texture: cannot load texture '%s'", path);
    pushEnt(L, e);
    return 1;
}

int SceneScript::lMaterial(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const char* name = luaL_checkstring(L, 1);
    auto color = readV3(L, 2);
    const float metallic = (float)luaL_optnumber(L, 3, 0.0);
    const float roughness = (float)luaL_optnumber(L, 4, 0.5);
    entt::entity tex = entt::null;
    if (!lua_isnoneornil(L, 5)) {
        tex = readEnt(L, 5);
        if (!w.reg.try_get<Texture>(tex))
            return luaL_error(L, "scene.material '%s': texture must be a texture entity (render.texture)", name);
    }
    const V3 emissive = readV3(L, 6);
    entt::entity e = findNamedEntity(w, name);
    if (e == entt::null || !w.reg.try_get<Material>(e))
        e = spawnMaterial(w, color, metallic, roughness, emissive, name);
    if (tex != entt::null)
        w.reg.get<Material>(e).tex = tex;
    pushEnt(L, e);
    return 1;
}

int SceneScript::lGeometry(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const Mesh m = checkGeom(L, "scene.geometry");
    lua_pushinteger(L, w.renderer.addMesh(m).geom);
    return 1;
}

// {field = number | boolean | {n, ...}} at idx -> CompFields (struct-form vocabulary); sets err
bool SceneScript::readCompFields(lua_State* L, int idx, CompFields& out, std::string& err) {
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        const char* name = luaL_checkstring(L, -2);
        CompField f;
        f.name = name;
        if (lua_isboolean(L, -1)) {
            f.v[0] = lua_toboolean(L, -1) ? 1.0f : 0.0f;
            f.n = 1;
        } else if (lua_isnumber(L, -1)) {
            f.v[0] = (float)lua_tonumber(L, -1);
            f.n = 1;
        } else if (lua_istable(L, -1)) {
            for (int i = 1; i <= 4; ++i) {
                lua_geti(L, -1, i);
                if (!lua_isnumber(L, -1)) {
                    lua_pop(L, 1);
                    break;
                }
                f.v[i - 1] = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
                f.n = i;
            }
        } else {
            lua_pop(L, 1);
            err = "field '" + std::string(name) + "' must be a number, boolean, or array";
            return false;
        }
        out.push_back(f);
        lua_pop(L, 1); // value; the key stays for lua_next
    }
    return true;
}

int SceneScript::lEntity(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = w.reg.create();
    const char* name = luaL_optstring(L, 1, nullptr);
    if (name)
        w.reg.emplace<Name>(e, name);
    pushEnt(L, e);
    return 1;
}

int SceneScript::lCompEmplace(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    CompFields fields;
    std::string err;
    if (lua_istable(L, 3) && !readCompFields(L, 3, fields, err))
        return luaL_error(L, "scene.emplace %s: %s", type.c_str(), err.c_str());
    if (!compEmplace(w.reg, e, type, fields, err))
        return luaL_error(L, "scene.emplace %s: %s", type.c_str(), err.c_str());
    return 0;
}

int SceneScript::lCompReplace(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    CompFields fields;
    std::string err;
    if (lua_istable(L, 3) && !readCompFields(L, 3, fields, err))
        return luaL_error(L, "scene.replace %s: %s", type.c_str(), err.c_str());
    if (!compReplace(w.reg, e, type, fields, err))
        return luaL_error(L, "scene.replace %s: %s", type.c_str(), err.c_str());
    return 0;
}

int SceneScript::lComponent(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    CompFields out;
    if (!compGet(w.reg, e, type, out)) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    for (const auto& f : out) {
        if (f.n == 1) {
            lua_pushnumber(L, f.v[0]);
        } else {
            lua_createtable(L, f.n, 0);
            for (int i = 0; i < f.n; ++i) {
                lua_pushnumber(L, f.v[i]);
                lua_seti(L, -2, i + 1);
            }
        }
        lua_setfield(L, -2, f.name.c_str());
    }
    return 1;
}

int SceneScript::lCompRemove(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    std::string err;
    if (!compRemove(w.reg, e, type, err))
        return luaL_error(L, "scene.remove %s: %s", type.c_str(), err.c_str());
    return 0;
}

int SceneScript::lMesh(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    const int geom = (int)luaL_checkinteger(L, 2);
    const char* matName = luaL_checkstring(L, 3);
    const bool hasOpts = lua_istable(L, 6);
    const entt::entity parent = hasOpts ? optEnt(L, 6, "parent") : entt::null;
    const bool noShadow = hasOpts ? optBool(L, 6, "noShadow") : false;
    const entt::entity mat = findNamedEntity(w, matName);
    if (mat == entt::null)
        return luaL_error(L, "render.mesh: unknown material '%s'", matName);
    // placement: create-or-assign the Transform with the given fields; the mesh core ensures a
    // pose (identity) when none is given
    CompFields tf;
    if (lua_istable(L, 4)) {
        const V3 pos = readV3(L, 4);
        tf.push_back(CompField{"pos", {pos.x, pos.y, pos.z}, 3});
    }
    if (lua_istable(L, 5)) {
        const V4 quat = readQuat(L, 5);
        tf.push_back(CompField{"quat", {quat.x, quat.y, quat.z, quat.w}, 4});
    }
    if (!tf.empty()) {
        std::string err;
        if (!compReplace(w.reg, e, "Transform", tf, err))
            return luaL_error(L, "render.mesh: %s", err.c_str());
    }
    addMesh(w, e, geom, mat, parent, noShadow);
    pushEnt(L, e);
    return 1;
}

// table on top of the stack: its ith numeric value
static float tableNum(lua_State* L, int i) {
    lua_geti(L, -1, i);
    const float v = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

// one collider spec: {shape = <shape entity>}, optionally at = {x, y, z} (local offset) and
// rot = {x, y, z, w} (local rotation)
static ColliderSpec readColliderSpec(World& w, lua_State* L, int idx) {
    ColliderSpec d{};
    const int t = lua_absindex(L, idx); // absolute: field pushes shift negative indexes
    lua_getfield(L, t, "shape");
    if (!lua_isinteger(L, -1)) {
        lua_pop(L, 1);
        luaL_error(L, "physics: collider needs shape = <shape entity>");
    }
    d.shape = readEnt(L, -1);
    lua_pop(L, 1);
    if (!w.reg.try_get<CapsuleShape>(d.shape) && !w.reg.try_get<BoxShape>(d.shape))
        luaL_error(L, "physics: shape entity has no capsule/box shape");
    lua_getfield(L, t, "at");
    if (lua_istable(L, -1)) {
        d.local.pos = V3{tableNum(L, 1), tableNum(L, 2), tableNum(L, 3)};
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }
    lua_getfield(L, t, "rot");
    if (lua_istable(L, -1)) {
        d.local.quat = V4{tableNum(L, 1), tableNum(L, 2), tableNum(L, 3), tableNum(L, 4)};
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }
    return d;
}

// collider list at idx: a list of {shape = ..., at?, rot?} specs
static std::vector<ColliderSpec> readColliderList(World& w, lua_State* L, int idx, const char* what) {
    std::vector<ColliderSpec> out;
    if (!lua_istable(L, idx))
        luaL_error(L, "%s: colliders must be a list of {shape = ...} specs", what);
    for (lua_Integer i = 1;; ++i) {
        lua_geti(L, idx, i);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        out.push_back(readColliderSpec(w, L, -1));
        lua_pop(L, 1);
    }
    if (out.empty())
        luaL_error(L, "%s: at least one collider", what);
    return out;
}

int SceneScript::lCapsuleShape(lua_State* L) {
    auto* self = sSelf(L);
    entt::entity e = self->w().reg.create();
    self->w().reg.emplace<CapsuleShape>(e, (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2));
    pushEnt(L, e);
    return 1;
}

int SceneScript::lBoxShape(lua_State* L) {
    auto* self = sSelf(L);
    entt::entity e = self->w().reg.create();
    self->w().reg.emplace<BoxShape>(
        e, V3{(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)});
    pushEnt(L, e);
    return 1;
}

int SceneScript::lBody(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    if (!w.reg.try_get<Transform>(e))
        return luaL_error(L, "physics.body: entity has no transform (add a mesh or set a pose first)");
    const std::vector<ColliderSpec> colliders = readColliderList(w, L, 2, "physics.body");
    bool isStatic = false;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "static");
        isStatic = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    addRigidBody(w, e, colliders, isStatic);
    pushEnt(L, e);
    return 1;
}

int SceneScript::lKinematic(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    if (!w.reg.try_get<Transform>(e))
        return luaL_error(L, "physics.kinematic: entity has no transform");
    addKinematicBody(w, e, readColliderList(w, L, 2, "physics.kinematic"));
    pushEnt(L, e);
    return 1;
}

int SceneScript::lSoftBody(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    std::string err;
    const Mesh m = readGeom(L, 2, err);
    if (!err.empty())
        return luaL_error(L, "soft.body: %s", err.c_str());
    std::vector<sim::Constraint> cons = readCons(L, 3, m.vertexCount(), err);
    if (!err.empty())
        return luaL_error(L, "soft.body: %s", err.c_str());
    const char* matName = luaL_checkstring(L, 4);
    const float mass = (float)luaL_optnumber(L, 5, 0.0f); // 0 = keep the default
    const entt::entity mat = findNamedEntity(w, matName);
    if (mat == entt::null)
        return luaL_error(L, "soft.body: unknown material '%s'", matName);
    addSoftBody(w, e, m, cons, mat, mass);
    pushEnt(L, e);
    return 1;
}

int SceneScript::lSoftFloor(lua_State* L) {
    auto* self = sSelf(L);
    self->w().sim.setFloor((float)luaL_checknumber(L, 1));
    return 0;
}

int SceneScript::lSet(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    // value(s) at arg idx: a number, or a {n, ...} array
    const auto readVals = [L](int idx) {
        std::vector<float> vals;
        if (lua_istable(L, idx)) {
            for (int i = 1;; ++i) {
                lua_geti(L, idx, i);
                if (lua_isnoneornil(L, -1)) {
                    lua_pop(L, 1);
                    break;
                }
                vals.push_back((float)lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
        } else {
            vals.push_back((float)luaL_checknumber(L, idx));
        }
        return vals;
    };
    std::string err;
    if (lua_isnumber(L, 1)) { // handle form: set(entity, field, value | {values})
        const entt::entity e = readEnt(L, 1);
        const std::string field = luaL_checkstring(L, 2);
        const auto vals = readVals(3);
        if (vals.empty() || !sceneSetEnt(w, e, field, vals.data(), (int)vals.size(), err))
            return luaL_error(L, "scene.set %s: %s", field.c_str(), err.c_str());
        return 0;
    }
    const std::string url = luaL_checkstring(L, 1); // url form: set("name.field", value | {values})
    const auto vals = readVals(2);
    if (vals.empty() || !sceneSet(w, url, vals.data(), (int)vals.size(), err))
        return luaL_error(L, "scene.set '%s': %s", url.c_str(), err.c_str());
    return 0;
}

int SceneScript::lGet(lua_State* L) {
    auto* self = sSelf(L);
    const World& w = self->w();
    float vals[3]{};
    int n = 0;
    std::string err;
    const bool ok = lua_isnumber(L, 1)
                        ? sceneGetEnt(w, readEnt(L, 1), luaL_checkstring(L, 2), vals, 3, n, err) // get(entity, field)
                        : sceneGet(w, luaL_checkstring(L, 1), vals, 3, n, err);                  // get("name.field")
    if (!ok)
        return luaL_error(L, "scene.get: %s", err.c_str());
    if (n == 1) {
        lua_pushnumber(L, vals[0]);
        return 1;
    }
    pushV3(L, {vals[0], vals[1], vals[2]});
    return 1;
}

int SceneScript::lTime(lua_State* L) {
    lua_pushnumber(L, sSelf(L)->m_time);
    return 1;
}

int SceneScript::lButton(lua_State* L) {
    auto* self = sSelf(L);
    const std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    self->m_buttons[name] = self->newRef(L, 2);
    return 0;
}

int SceneScript::lAttachScript(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    const std::string file = luaL_checkstring(L, 2);
    if (!w.reg.valid(e))
        return luaL_error(L, "scene.attach_script: invalid entity");
    auto* p = w.reg.try_get<LuaScript>(e);
    LuaScript& ls = p ? *p : w.reg.emplace<LuaScript>(e);
    if (ls.ready) { // re-attach: tear down the old script first
        self->destroyScript(ls.inst, ls.params, ls.onReady, ls.animate, ls.physics);
        ls = LuaScript{};
    }
    ls.file = file;
    if (lua_istable(L, 3))
        ls.params = self->newRef(L, 3);
    return 0;
}

// an entity and all its descendants (children, grandchildren, ...)
static std::vector<entt::entity> subtreeEntities(entt::registry& reg, entt::entity root) {
    std::map<unsigned, std::vector<entt::entity>> children;
    for (auto [c, p] : reg.view<Parent>().each())
        children[entt::to_integral(p.e)].push_back(c);
    std::vector<entt::entity> out;
    std::vector<entt::entity> stack{root};
    while (!stack.empty()) {
        const auto cur = stack.back();
        stack.pop_back();
        out.push_back(cur);
        if (const auto it = children.find(entt::to_integral(cur)); it != children.end())
            for (const auto c : it->second)
                stack.push_back(c);
    }
    return out;
}

int SceneScript::lDestroy(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    if (!w.reg.valid(e))
        return 0;
    if (w.reg.try_get<CapsuleShape>(e) || w.reg.try_get<BoxShape>(e))
        for (auto [c, col] : w.reg.view<Collider>().each())
            if (col.shape == e)
                return luaL_error(L, "scene.destroy: shape is referenced by a collider");
    if (w.reg.try_get<Texture>(e))
        for (auto [m, mat] : w.reg.view<Material>().each())
            if (mat.tex == e)
                return luaL_error(L, "scene.destroy: texture is referenced by a material");
    const auto subtree = subtreeEntities(w.reg, e);
    for (const auto c : subtree)
        w.reg.destroy(c); // destroyed entities fall out of the renderer's ECS query; buffers freed at shutdown
    return 0;
}

int SceneScript::lAddChild(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity parent = readEnt(L, 1);
    const entt::entity child = readEnt(L, 2);
    if (!w.reg.valid(parent) || !w.reg.valid(child))
        return luaL_error(L, "scene.add_child: invalid entity");
    if (!w.reg.try_get<Transform>(parent))
        return luaL_error(L, "scene.add_child: parent has no Transform");
    bool keepWorld = false;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "keepWorld");
        keepWorld = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    if (keepWorld) {
        // preserve the child's current world pose: rewrite its Transform as local-to-parent
        if (!w.reg.try_get<Transform>(child))
            return luaL_error(L, "scene.add_child: keepWorld requires the child to have a Transform");
        const auto local =
            transformCompose(transformInvert(worldTransform(w.reg, parent)), worldTransform(w.reg, child));
        auto& ct = w.reg.get<Transform>(child);
        ct.pos = local.pos;
        ct.quat = local.quat;
    }
    w.reg.emplace_or_replace<Parent>(child, parent); // (re)interprets the child's Transform as local
    return 0;
}

int SceneScript::lRemoveChild(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity child = readEnt(L, 1);
    if (w.reg.valid(child))
        w.reg.remove<Parent>(child);
    return 0;
}

int SceneScript::lGetParent(lua_State* L) {
    auto* self = sSelf(L);
    const World& w = self->w();
    const entt::entity child = readEnt(L, 1);
    if (const auto* p = w.reg.try_get<Parent>(child)) {
        pushEnt(L, p->e);
        return 1;
    }
    return 0; // nil
}

int SceneScript::lGetChildren(lua_State* L) {
    auto* self = sSelf(L);
    const World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    lua_newtable(L);
    int i = 1;
    for (auto [c, p] : w.reg.view<Parent>().each())
        if (p.e == e) {
            lua_pushinteger(L, (lua_Integer)entt::to_integral(c));
            lua_seti(L, -2, i++);
        }
    return 1;
}

int SceneScript::lvAdd(lua_State* L) {
    pushV3(L, vAdd(readV3(L, 1), readV3(L, 2)));
    return 1;
}

int SceneScript::lvSub(lua_State* L) {
    pushV3(L, vSub(readV3(L, 1), readV3(L, 2)));
    return 1;
}

int SceneScript::lvScale(lua_State* L) {
    pushV3(L, vScale(readV3(L, 1), (float)luaL_checknumber(L, 2)));
    return 1;
}

int SceneScript::lvLen(lua_State* L) {
    lua_pushnumber(L, vLen(readV3(L, 1)));
    return 1;
}

int SceneScript::lvNorm(lua_State* L) {
    pushV3(L, vNorm(readV3(L, 1)));
    return 1;
}

int SceneScript::lvCross(lua_State* L) {
    pushV3(L, vCross(readV3(L, 1), readV3(L, 2)));
    return 1;
}

int SceneScript::lvDot(lua_State* L) {
    lua_pushnumber(L, vDot(readV3(L, 1), readV3(L, 2)));
    return 1;
}

int SceneScript::lqMul(lua_State* L) {
    pushV4(L, quatMul(readQuat(L, 1), readQuat(L, 2)));
    return 1;
}

int SceneScript::lqInv(lua_State* L) {
    pushV4(L, quatInv(readQuat(L, 1)));
    return 1;
}

int SceneScript::lqAxis(lua_State* L) {
    pushV4(L, quatAxisAngle(readV3(L, 1), (float)luaL_checknumber(L, 2)));
    return 1;
}

int SceneScript::lqRot(lua_State* L) {
    pushV3(L, quatRotate(readQuat(L, 1), readV3(L, 2)));
    return 1;
}

int SceneScript::lqFromTo(lua_State* L) {
    pushV4(L, quatFromTo(readV3(L, 1), readV3(L, 2)));
    return 1;
}

int SceneScript::lFind(lua_State* L) {
    auto* self = sSelf(L);
    const std::string name = luaL_checkstring(L, 1);
    const entt::entity e = findNamedEntity(self->w(), name);
    if (e == entt::null) {
        lua_pushnil(L);
        return 1;
    }
    pushEnt(L, e);
    return 1;
}

int SceneScript::lIsVr(lua_State* L) {
    auto* self = sSelf(L);
    lua_pushboolean(L, self->w().xrFrame.havePose && self->w().xrFrame.nEyes > 0);
    return 1;
}

int SceneScript::lKeyDown(lua_State* L) {
    auto* self = sSelf(L);
    const entt::entity e = readEnt(L, 1);
    const int code = (int)luaL_checknumber(L, 2);
    if (code < 0 || code >= KeyboardInput::kMaxKeys) {
        lua_pushboolean(L, false);
        return 1;
    }
    const auto* ki = self->w().reg.try_get<KeyboardInput>(e);
    lua_pushboolean(L, ki && ki->keys[code]);
    return 1;
}

int SceneScript::lRaycast(lua_State* L) {
    auto* self = sSelf(L);
    const V3 origin = readV3(L, 1);
    const V3 dir = readV3(L, 2);
    const float maxDist = (float)luaL_checknumber(L, 3);
    // optional arg 4: a table of entities whose bodies (dynamic or kinematic) the ray skips
    std::vector<JPH::BodyID> excludes;
    if (lua_istable(L, 4))
        for (int i = 1;; ++i) {
            lua_geti(L, 4, i);
            if (lua_isnoneornil(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            const entt::entity e = readEnt(L, -1);
            if (auto* b = self->w().reg.try_get<RigidBody>(e))
                excludes.push_back(b->id);
            else if (auto* k = self->w().reg.try_get<KinematicBody>(e))
                excludes.push_back(k->id);
            lua_pop(L, 1);
        }
    const RayHit hit = self->w().rigid.raycast(origin, dir, maxDist, excludes);
    if (!hit.hit) {
        lua_pushnil(L);
        return 1;
    }
    // find the ECS entity for the hit body
    entt::entity bodyEnt = entt::null;
    for (auto [e, body] : self->w().reg.view<RigidBody>().each())
        if (body.id == hit.body)
            bodyEnt = e;
    lua_newtable(L);
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "hit");
    pushV3(L, hit.pos);
    lua_setfield(L, -2, "pos");
    lua_pushnumber(L, maxDist); // dist (approx; Jolt doesn't return it directly in RayCastResult)
    lua_setfield(L, -2, "dist");
    if (bodyEnt != entt::null) {
        pushEnt(L, bodyEnt);
        lua_setfield(L, -2, "entity");
    }
    return 1;
}

int SceneScript::lCreateJoint(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity eA = readEnt(L, 1);
    const entt::entity eB = readEnt(L, 2);
    const char* type = luaL_checkstring(L, 3);
    // resolve Jolt body IDs: from RigidBody or KinematicBody
    auto getBodyId = [&](entt::entity e) -> JPH::BodyID {
        if (auto* b = w.reg.try_get<RigidBody>(e))
            return b->id;
        if (auto* k = w.reg.try_get<KinematicBody>(e))
            return k->id;
        return {};
    };
    const JPH::BodyID idA = getBodyId(eA);
    const JPH::BodyID idB = getBodyId(eB);
    if (idA.IsInvalid() && idB.IsInvalid())
        return luaL_error(L, "scene.create_joint: entity has no rigid/kinematic body");
    const V3 grabPos = readV3(L, 4); // world collision point the joint anchors
    const int jid = w.rigid.createJoint(idA, idB, type, grabPos);
    if (jid < 0)
        return luaL_error(L, "scene.create_joint: unknown type '%s'", type);
    lua_pushnumber(L, jid);
    return 1;
}

int SceneScript::lDestroyJoint(lua_State* L) {
    auto* self = sSelf(L);
    self->w().rigid.destroyJoint((int)luaL_checknumber(L, 1));
    return 0;
}

int SceneScript::lIsDynamic(lua_State* L) {
    auto* self = sSelf(L);
    const entt::entity e = readEnt(L, 1);
    const auto* b = self->w().reg.try_get<RigidBody>(e);
    lua_pushboolean(L, b && self->w().rigid.isDynamic(b->id));
    return 1;
}
