#include "scene_script.hpp"

#include "assets.hpp"
#include "systems.hpp"
#include "url.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

// named field of an opts table, with default
float optNum(lua_State* L, int tidx, const char* key, float def) {
    lua_getfield(L, tidx, key);
    const float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
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
    reg("capsule", lCapsule);   // capsule(halfLen, radius, mat, pos?, quat?, name?, {body?, parent?}?)
    reg("cloth", lCloth);       // cloth(cw, ch, span, y0, mat, pin?, name?)
    reg("ball", lBall);         // ball(subdiv, radius, mat, pos?, mass?, name?)
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
    lua_pop(L, 1);
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

int SceneScript::lCapsule(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const CapsuleParams shape{(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)};
    const char* matName = luaL_checkstring(L, 3);
    auto pos = readV3(L, 4);
    auto quat = readQuat(L, 5);
    const char* name = luaL_optstring(L, 6, nullptr);
    const std::string body = lua_istable(L, 7) ? optString(L, 7, "body", "dynamic") : "dynamic";
    const entt::entity parent = lua_istable(L, 7) ? optEnt(L, 7, "parent") : entt::null;
    const entt::entity mat = findNamedEntity(w, matName);
    if (mat == entt::null)
        return luaL_error(L, "scene.capsule: unknown material '%s'", matName);
    entt::entity e;
    if (body == "kinematic")
        e = spawnVisualCapsule(w, shape, {pos, quat}, mat, parent);
    else
        e = spawnDynamicCapsule(w, shape, {pos, quat}, mat);
    if (name)
        w.reg.emplace<Name>(e, name);
    pushEnt(L, e);
    return 1;
}

int SceneScript::lCloth(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const int cw = (int)luaL_checkinteger(L, 1);
    const int ch = (int)luaL_checkinteger(L, 2);
    const float span = (float)luaL_checknumber(L, 3);
    const float y0 = (float)luaL_checknumber(L, 4);
    const char* matName = luaL_checkstring(L, 5);
    const float pin = (float)luaL_optnumber(L, 6, 0.0f);
    const char* name = luaL_optstring(L, 7, nullptr);
    const entt::entity mat = findNamedEntity(w, matName);
    if (mat == entt::null)
        return luaL_error(L, "scene.cloth: unknown material '%s'", matName);
    const entt::entity e = spawnSoftCloth(w, cw, ch, span, y0, mat, pin);
    if (name)
        w.reg.emplace<Name>(e, name);
    pushEnt(L, e);
    return 1;
}

int SceneScript::lBall(lua_State* L) {
    auto* self = sSelf(L);
    World& w = self->w();
    const int subdiv = (int)luaL_checkinteger(L, 1);
    const float radius = (float)luaL_checknumber(L, 2);
    const char* matName = luaL_checkstring(L, 3);
    auto pos = readV3(L, 4);
    const float mass = (float)luaL_optnumber(L, 5, 0.0f);
    const char* name = luaL_optstring(L, 6, nullptr);
    const entt::entity mat = findNamedEntity(w, matName);
    if (mat == entt::null)
        return luaL_error(L, "scene.ball: unknown material '%s'", matName);
    const entt::entity e = spawnSoftBall(w, subdiv, radius, pos, mat, mass);
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
