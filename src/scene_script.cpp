#include "scene_script.hpp"

#include "assets.hpp"
#include "systems.hpp"
#include "url.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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

// the assets dir (cwd or exe-relative) — root for restricted includes
std::string assetsRoot() {
    return std::filesystem::exists("assets") ? "assets" : exeDir() + "/../assets";
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

std::string optString(lua_State* L, int tidx, const char* key, const char* def) {
    lua_getfield(L, tidx, key);
    const std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

entt::entity optEnt(lua_State* L, int tidx, const char* key) {
    lua_getfield(L, tidx, key);
    const entt::entity e = lua_isnumber(L, -1) ? readEnt(L, -1) : entt::null;
    lua_pop(L, 1);
    return e;
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
    lua_newtable(L); // the scene table, stays on the stack while verbs are registered
    const auto reg = [this](const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, fn, 1); // pops the upvalue: stack is [scene, closure]
        lua_setfield(L, -2, name);
    };
    reg("material", lMaterial); // material(name, color, metallic, roughness, texture?)
    reg("geometry", lGeometry); // geometry(geomTable) -> geom id
    reg("capsule", lCapsule);   // capsule(geom, halfLen, radius, mat, pos?, quat?, name?, {body?, parent?}?)
    reg("cloth", lCloth);       // cloth(geom, cons, mat, pin?, name?)
    reg("ball", lBall);         // ball(geom, cons, mat, mass?, name?)
    reg("set", lSet);           // set(url, value | {values})
    reg("get", lGet);           // get(url) -> number | {x, y, z}
    reg("time", lTime);         // time() -> seconds since scene start
    reg("entity", lEntity);     // entity(name) -> handle
    reg("frame", lFrame);       // frame(fn(dt))
    reg("anim", lAnim);         // anim(entity, fn(dt) -> pos, quat)
    reg("button", lButton);     // button(name, fn())
    reg("reset_softs", lResetSofts);
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
    lua_setglobal(L, "scene");
    // global module loader (restricted to the assets dir, cached per path)
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, lInclude, 1);
    lua_setglobal(L, "include");
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

int SceneScript::lInclude(lua_State* L) {
    auto* self = sSelf(L);
    const char* p = luaL_checkstring(L, 1);
    std::string err;
    const int ref = self->includeModule(p, err);
    if (ref < 0)
        return luaL_error(L, "include: %s", err.c_str());
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    return 1;
}

bool SceneScript::run(const std::string& path, std::string& err) {
    if (luaL_loadfile(L, path.c_str()) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
        err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        std::fprintf(stderr, "3dsim: scene script %s: %s\n", path.c_str(), err.c_str());
        lua_pop(L, 1);
        return false;
    }
    return true;
}

int SceneScript::callMeshGen(const std::string& gen, const std::vector<std::pair<std::string, double>>& params,
                             std::string& err) {
    if (m_geomModule < 0) {
        err = "no geometry module (scene has no \"meshlib\")";
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

void SceneScript::frame(float dt) {
    if (!L)
        return;
    m_time += dt;
    for (size_t i = 0; i < m_frames.size();) {
        const int ref = m_frames[i];
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_pushnumber(L, dt);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            reportError("frame callback");
            dropRef(ref);
            m_frames.erase(m_frames.begin() + (std::ptrdiff_t)i);
            continue;
        }
        ++i;
    }
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

void SceneScript::shutdown() {
    if (!L)
        return;
    for (int ref : m_refs)
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    m_refs.clear();
    m_frames.clear();
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

bool SceneScript::callPoseRef(int ref, float dt, V3& pos, V4& quat) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushnumber(L, dt);
    if (lua_pcall(L, 1, 2, 0) != LUA_OK) {
        if (std::ranges::find(m_errored, ref) == m_errored.end()) {
            m_errored.push_back(ref);
            reportError("anim callback");
        } else {
            lua_pop(L, 1); // error object
        }
        return false;
    }
    pos = readV3(L, -2);
    quat = readQuat(L, -1);
    lua_pop(L, 2);
    return true;
}

void SceneScript::reportError(const char* what) {
    luaL_traceback(L, L, lua_tostring(L, -1), 1);
    const char* tb = lua_tostring(L, -1);
    std::fprintf(stderr, "3dsim: script %s: %s\n", what, tb ? tb : "?");
    lua_pop(L, 2); // traceback + error object
}

int SceneScript::lMaterial(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const char* name = luaL_checkstring(L, 1);
    auto color = readV3(L, 2);
    const float metallic = (float)luaL_optnumber(L, 3, 0.0);
    const float roughness = (float)luaL_optnumber(L, 4, 0.5);
    const char* tex = luaL_optstring(L, 5, nullptr);
    entt::entity e = findNamedEntity(w, name);
    if (e == entt::null || !w.reg.try_get<Material>(e))
        e = spawnMaterial(w, color, metallic, roughness, name);
    if (tex) {
        const int texId = loadAssetTexture(w, tex);
        if (texId < 0)
            return luaL_error(L, "scene.material '%s': cannot load texture '%s'", name, tex);
        w.reg.get<Material>(e).texId = texId;
    }
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

int SceneScript::lCapsule(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const int geom = (int)luaL_checkinteger(L, 1);
    const CapsuleParams shape{(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)};
    const char* matName = luaL_checkstring(L, 4);
    auto pos = readV3(L, 5);
    auto quat = readQuat(L, 6);
    const char* name = luaL_optstring(L, 7, nullptr);
    const std::string body = lua_istable(L, 8) ? optString(L, 8, "body", "dynamic") : "dynamic";
    const entt::entity parent = lua_istable(L, 8) ? optEnt(L, 8, "parent") : entt::null;
    const entt::entity mat = findNamedEntity(w, matName);
    if (mat == entt::null)
        return luaL_error(L, "scene.capsule: unknown material '%s'", matName);
    entt::entity e;
    if (body == "kinematic")
        e = spawnVisualCapsule(w, geom, {pos, quat}, mat, parent);
    else
        e = spawnDynamicCapsule(w, geom, shape, {pos, quat}, mat);
    if (name)
        w.reg.emplace<Name>(e, name);
    pushEnt(L, e);
    return 1;
}

int SceneScript::lCloth(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const Mesh m = checkGeom(L, "scene.cloth");
    std::string err;
    std::vector<sim::Constraint> cons = readCons(L, 2, m.vertexCount(), err);
    if (!err.empty())
        return luaL_error(L, "scene.cloth: %s", err.c_str());
    const char* matName = luaL_checkstring(L, 3);
    const float pin = (float)luaL_optnumber(L, 4, 0.0f);
    const char* name = luaL_optstring(L, 5, nullptr);
    const entt::entity mat = findNamedEntity(w, matName);
    if (mat == entt::null)
        return luaL_error(L, "scene.cloth: unknown material '%s'", matName);
    const entt::entity e = spawnSoftCloth(w, m, cons, mat, pin);
    if (name)
        w.reg.emplace<Name>(e, name);
    pushEnt(L, e);
    return 1;
}

int SceneScript::lBall(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const Mesh m = checkGeom(L, "scene.ball");
    std::string err;
    std::vector<sim::Constraint> cons = readCons(L, 2, m.vertexCount(), err);
    if (!err.empty())
        return luaL_error(L, "scene.ball: %s", err.c_str());
    const char* matName = luaL_checkstring(L, 3);
    const float mass = (float)luaL_optnumber(L, 4, 0.0f);
    const char* name = luaL_optstring(L, 5, nullptr);
    const entt::entity mat = findNamedEntity(w, matName);
    if (mat == entt::null)
        return luaL_error(L, "scene.ball: unknown material '%s'", matName);
    const entt::entity e = spawnSoftBall(w, m, cons, mat, mass);
    if (name)
        w.reg.emplace<Name>(e, name);
    pushEnt(L, e);
    return 1;
}

int SceneScript::lSet(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const std::string url = luaL_checkstring(L, 1);
    std::vector<float> vals;
    if (lua_istable(L, 2)) {
        for (int i = 1;; ++i) {
            lua_geti(L, 2, i);
            if (lua_isnoneornil(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            vals.push_back((float)lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
    } else {
        vals.push_back((float)luaL_checknumber(L, 2));
    }
    std::string err;
    if (vals.empty() || !sceneSet(w, url, vals.data(), (int)vals.size(), err))
        return luaL_error(L, "scene.set '%s': %s", url.c_str(), err.c_str());
    return 0;
}

int SceneScript::lGet(lua_State* L) {
    auto* self = sSelf(L);
    const World& w = self->w();
    const std::string url = luaL_checkstring(L, 1);
    float vals[3]{};
    int n = 0;
    std::string err;
    if (!sceneGet(w, url, vals, 3, n, err))
        return luaL_error(L, "scene.get '%s': %s", url.c_str(), err.c_str());
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

int SceneScript::lEntity(lua_State* L) {
    auto* self = sSelf(L);
    const std::string name = luaL_checkstring(L, 1);
    const entt::entity e = findNamedEntity(self->w(), name);
    if (e == entt::null)
        return luaL_error(L, "scene.entity: no entity named '%s'", name.c_str());
    pushEnt(L, e);
    return 1;
}

int SceneScript::lFrame(lua_State* L) {
    auto* self = sSelf(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    self->m_frames.push_back(self->newRef(L, 1));
    return 0;
}

int SceneScript::lAnim(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const entt::entity e = readEnt(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const int ref = self->newRef(L, 2);
    w.reg.emplace_or_replace<Animation>(e, [self, ref](Transform& t, float dt) {
        V3 pos;
        V4 quat;
        if (self->callPoseRef(ref, dt, pos, quat)) {
            t.pos = pos;
            t.quat = quat;
        }
    });
    return 0;
}

int SceneScript::lButton(lua_State* L) {
    auto* self = sSelf(L);
    const std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    self->m_buttons[name] = self->newRef(L, 2);
    return 0;
}

int SceneScript::lResetSofts(lua_State* L) {
    resetSofts(sSelf(L)->w());
    return 0;
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
