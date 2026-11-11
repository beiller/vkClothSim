#pragma once
#include "ecs.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include <lua.hpp>
#include <string>
#include <vector>

struct LuaTable {
    lua_State* L;
    int idx;
    int arr = 0;
    explicit LuaTable(lua_State* L, int narr = 0, int nrec = 0) : L(L) {
        lua_createtable(L, narr, nrec);
        idx = lua_gettop(L);
    }
    void setStr(const char* k, const std::string& v) const { lua_pushstring(L, v.c_str()); lua_setfield(L, idx, k); }
    void setNum(const char* k, double v) const { lua_pushnumber(L, v); lua_setfield(L, idx, k); }
    void setBool(const char* k, bool v) const { lua_pushboolean(L, v); lua_setfield(L, idx, k); }
    void appendNum(double v) { lua_pushnumber(L, v); lua_rawseti(L, idx, ++arr); }
    void appendStr(const std::string& v) { lua_pushstring(L, v.c_str()); lua_rawseti(L, idx, ++arr); }
    void setTable(const char* k, int subIdx) const { lua_pushvalue(L, subIdx); lua_setfield(L, idx, k); }
    void appendTable(int subIdx) { lua_pushvalue(L, subIdx); lua_rawseti(L, idx, ++arr); }
};

inline entt::entity luaReadEnt(lua_State* L, int idx) {
    return static_cast<entt::entity>((uint32_t)luaL_checkinteger(L, idx));
}

inline void luaPushEnt(lua_State* L, entt::entity e) {
    lua_pushinteger(L, (lua_Integer)entt::to_integral(e));
}

inline float luaReadNum(lua_State* L, int tidx, int i, float def) {
    lua_geti(L, tidx, i);
    const float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

inline V3 luaReadV3(lua_State* L, int idx, const V3& def = {0, 0, 0}) {
    if (!lua_istable(L, idx))
        return def;
    return {luaReadNum(L, idx, 1, def.x), luaReadNum(L, idx, 2, def.y), luaReadNum(L, idx, 3, def.z)};
}

inline Quat luaReadQuat(lua_State* L, int idx) {
    if (!lua_istable(L, idx))
        return {1.0f, 0.0f, 0.0f, 0.0f};
    const float x = luaReadNum(L, idx, 1, 0);
    const float y = luaReadNum(L, idx, 2, 0);
    const float z = luaReadNum(L, idx, 3, 0);
    const float w = luaReadNum(L, idx, 4, 1);
    return {w, x, y, z};
}

template <int N>
void luaPushVecN(lua_State* L, const float* v) {
    lua_createtable(L, N, 0);
    for (int i = 0; i < N; ++i) {
        lua_pushnumber(L, v[i]); // NOLINT(clang-analyzer-security.ArrayBound): caller guarantees N elements
        lua_seti(L, -2, i + 1);
    }
}

inline void luaPushV3(lua_State* L, V3 v) { luaPushVecN<3>(L, &v.x); }
inline void luaPushV4(lua_State* L, V4 q) {
    const float v[4] = {q.x, q.y, q.z, q.w};
    luaPushVecN<4>(L, v);
}

inline void luaPushQuat(lua_State* L, Quat q) {
    const float v[4] = {q.x, q.y, q.z, q.w};
    luaPushVecN<4>(L, v);
}

template <class T>
inline bool luaReadNumArray(lua_State* L, int absidx, std::vector<T>& out, std::string& err) {
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

template <class T>
inline bool luaReadGeomField(lua_State* L, int tidx, const char* name, size_t per, std::vector<T>& out, bool required,
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
    if (!luaReadNumArray(L, lua_absindex(L, -1), out, err)) {
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

bool readCompFields(lua_State* L, int idx, CompFields& out, std::string& err, const char* skipField);

inline bool luaReadShapesTable(lua_State* L, int idx, int nVerts, std::vector<SubMesh>& out, std::string& err) {
    lua_getfield(L, idx, "shapes");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    std::vector<SubMesh> list;
    for (int i = 1;; ++i) {
        lua_geti(L, -1, i);
        if (lua_isnoneornil(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        if (!lua_istable(L, -1)) {
            err = "geometry: shapes entry must be a table";
            lua_pop(L, 2);
            return false;
        }
        SubMesh sh;
        lua_getfield(L, -1, "name");
        const bool hasName = lua_isstring(L, -1);
        if (hasName)
            sh.name = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (!hasName) {
            err = "geometry: shape needs a string 'name'";
            lua_pop(L, 2);
            return false;
        }
        lua_getfield(L, -1, "weight");
        if (lua_isnumber(L, -1))
            sh.weight = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        if (!luaReadGeomField(L, -1, "pos", 3, sh.pos, false, err)) {
            lua_pop(L, 2);
            return false;
        }
        if (!luaReadGeomField(L, -1, "nrm", 3, sh.nrm, false, err)) {
            lua_pop(L, 2);
            return false;
        }
        if (!sh.pos.empty() && sh.pos.size() != (size_t)3 * nVerts) {
            err = "geometry: shape 'pos' must have 3 * vertexCount entries";
            lua_pop(L, 2);
            return false;
        }
        list.push_back(std::move(sh));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    out = std::move(list);
    return true;
}
