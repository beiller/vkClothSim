#pragma once
#include "ecs.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include <lua.hpp>
#include <string>
#include <vector>

// A table on the Lua stack: named fields via set*, 1-based array elements via
// append*. Sub-tables are attached by index (setTable/appendTable copy the
// value; the sub-table stays on the stack for the caller to pop or re-attach).
struct LuaTable {
    lua_State* L;
    int idx;
    int arr = 0;
    LuaTable(lua_State* L, int narr = 0, int nrec = 0) : L(L) {
        lua_createtable(L, narr, nrec);
        idx = lua_gettop(L);
    }
    void setStr(const char* k, const std::string& v) { lua_pushstring(L, v.c_str()); lua_setfield(L, idx, k); }
    void setNum(const char* k, double v) { lua_pushnumber(L, v); lua_setfield(L, idx, k); }
    void setBool(const char* k, bool v) { lua_pushboolean(L, v); lua_setfield(L, idx, k); }
    void appendNum(double v) { lua_pushnumber(L, v); lua_rawseti(L, idx, ++arr); }
    void appendStr(const std::string& v) { lua_pushstring(L, v.c_str()); lua_rawseti(L, idx, ++arr); }
    void setTable(const char* k, int subIdx) { lua_pushvalue(L, subIdx); lua_setfield(L, idx, k); }
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
        return Quat(1.0f, 0.0f, 0.0f, 0.0f); // identity (w first)
    // table is [x, y, z, w]; the Quat constructor is (w, x, y, z)
    const float x = luaReadNum(L, idx, 1, 0);
    const float y = luaReadNum(L, idx, 2, 0);
    const float z = luaReadNum(L, idx, 3, 0);
    const float w = luaReadNum(L, idx, 4, 1);
    return Quat(w, x, y, z);
}

inline void luaPushV3(lua_State* L, V3 v) {
    lua_createtable(L, 3, 0);
    lua_pushnumber(L, v.x);
    lua_seti(L, -2, 1);
    lua_pushnumber(L, v.y);
    lua_seti(L, -2, 2);
    lua_pushnumber(L, v.z);
    lua_seti(L, -2, 3);
}

inline void luaPushV4(lua_State* L, V4 q) {
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

// pushes a quat as an [x, y, z, w] table (the Quat ctor order is w-first, but the table is not)
inline void luaPushQuat(lua_State* L, Quat q) {
    luaPushV4(L, V4(q.x, q.y, q.z, q.w));
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

struct CompData {
    std::string type;
    CompFields fields;
};

inline void pushCompField(lua_State* L, const CompField& f) {
    if (f.isStr) {
        lua_pushstring(L, f.str.c_str());
    } else if (f.isStrArr) {
        lua_createtable(L, f.n, 0);
        for (int i = 0; i < f.n; ++i) {
            lua_pushstring(L, f.strArr[i].c_str());
            lua_seti(L, -2, i + 1);
        }
    } else if (f.hasEnt) {
        luaPushEnt(L, f.ent);
    } else if (!f.isArr && f.n == 1) {
        lua_pushnumber(L, f.v[0]);
    } else {
        const float* src = f.isArr ? f.arr.data() : f.v;
        lua_createtable(L, f.n, 0);
        for (int i = 0; i < f.n; ++i) {
            lua_pushnumber(L, src[i]);
            lua_seti(L, -2, i + 1);
        }
    }
}

static int compIndex(lua_State* L) {
    const CompData* d = static_cast<const CompData*>(lua_touserdata(L, 1));
    const char* name = luaL_checkstring(L, 2);
    for (const auto& f : d->fields)
        if (f.name == name) {
            pushCompField(L, f);
            return 1;
        }
    lua_pushnil(L);
    return 1;
}

static int compNewIndex(lua_State* L) {
    CompData* d = static_cast<CompData*>(lua_touserdata(L, 1));
    const char* name = luaL_checkstring(L, 2);
    if (lua_isnoneornil(L, 3)) {
        for (size_t i = 0; i < d->fields.size(); ++i)
            if (d->fields[i].name == name) {
                d->fields.erase(d->fields.begin() + i);
                return 0;
            }
        return 0;
    }
    CompField f;
    f.name = name;
    if (lua_isboolean(L, 3)) {
        f.v[0] = lua_toboolean(L, 3) ? 1.0f : 0.0f;
        f.n = 1;
    } else if (lua_isnumber(L, 3)) {
        f.v[0] = (float)lua_tonumber(L, 3);
        f.n = 1;
        if (lua_isinteger(L, 3)) {
            f.hasEnt = true;
            f.ent = luaReadEnt(L, 3);
        }
    } else if (lua_isstring(L, 3)) {
        f.isStr = true;
        f.str = lua_tostring(L, 3);
    } else if (lua_istable(L, 3)) {
        const int t = lua_absindex(L, 3);
        bool anyNum = false;
        for (int i = 1;; ++i) {
            lua_geti(L, t, i);
            if (lua_isnoneornil(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            if (lua_isnumber(L, -1)) {
                f.arr.push_back((float)lua_tonumber(L, -1));
                anyNum = true;
            } else if (lua_isstring(L, -1)) {
                f.strArr.push_back(lua_tostring(L, -1));
            } else {
                lua_pop(L, 1);
                luaL_error(L, "field '%s' must be an array of numbers or strings", name);
            }
            lua_pop(L, 1);
            f.n = i;
        }
        if (f.n == 0)
            luaL_error(L, "field '%s' must be a non-empty array", name);
        if (anyNum && !f.strArr.empty())
            luaL_error(L, "field '%s' mixes numbers and strings", name);
        f.isArr = anyNum;
        f.isStrArr = !anyNum;
    } else {
        luaL_error(L, "field '%s' must be a number, boolean, string, or array", name);
    }
    for (auto& g : d->fields)
        if (g.name == name) {
            g = std::move(f);
            return 0;
        }
    d->fields.push_back(std::move(f));
    return 0;
}

static int compGc(lua_State* L) {
    static_cast<CompData*>(lua_touserdata(L, 1))->~CompData();
    return 0;
}

inline void compMetaEnsure(lua_State* L) {
    const char* key = "3dsim.CompData.meta";
    lua_getfield(L, LUA_REGISTRYINDEX, key);
    if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pop(L, 1);
    luaL_newmetatable(L, "3dsim.CompData");
    lua_pushcfunction(L, compIndex);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, compNewIndex);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, compGc);
    lua_setfield(L, -2, "__gc");
    lua_setfield(L, LUA_REGISTRYINDEX, key);
}

inline void pushCompData(lua_State* L, const std::string& type, CompFields fields) {
    compMetaEnsure(L);
    // placement-new: the userdata memory is uninitialized and CompData has non-trivial members
    CompData* d = new (lua_newuserdata(L, sizeof(CompData))) CompData;
    d->type = type;
    d->fields = std::move(fields);
    luaL_setmetatable(L, "3dsim.CompData");
}

inline bool asCompData(lua_State* L, int idx, CompData** out) {
    *out = static_cast<CompData*>(luaL_testudata(L, idx, "3dsim.CompData"));
    return *out != nullptr;
}

// Reads an optional 'shapes' key (a list of {name, weight, pos, nrm}) from the table at idx.
// Leaves out untouched when the key is absent; otherwise out is the full list.
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
