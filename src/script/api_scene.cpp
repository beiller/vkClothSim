#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

#include "app/engine.hpp"
#include "assets.hpp"
#include "gltf.hpp"
#include "url.hpp"
#include "physics/mesh_decomp.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {

void reg(lua_State* L, SceneScript* host, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

/// scene.set(target: entity|string, field: string, value: number|table|string) | set a reflected field (URL form "Entity.field" or entity + field name); value is a number, an array of up to 4 numbers, or a string for "name.LuaScript.var" targets
int lSet(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    Engine& w = self->w();
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
    if (lua_isnumber(L, 1)) {
        const entt::entity e = luaReadEnt(L, 1);
        const std::string field = luaL_checkstring(L, 2);
        const auto vals = readVals(3);
        if (vals.empty() || !sceneSetEnt(w.reg, e, field, vals.data(), (int)vals.size(), err))
            return luaL_error(L, "scene.set %s: %s", field.c_str(), err.c_str());
        return 0;
    }
    const std::string url = luaL_checkstring(L, 1);
    if (lua_isstring(L, 2) && url.find(".LuaScript.") != std::string::npos) {
        self->setVarStr(w.reg, url, luaL_checkstring(L, 2), err);
        return 0;
    }
    const auto vals = readVals(2);
    if (vals.empty() || !sceneSet(w.reg, url, vals.data(), (int)vals.size(), err))
        return luaL_error(L, "scene.set '%s': %s", url.c_str(), err.c_str());
    return 0;
}

/// scene.get(target: entity|string, field?: string) -> number|vec3|vec4|string | read a reflected field (URL form "Entity.field", or entity + field name); returns a scalar, {x,y,z}, {x,y,z,w}, or a string for "name.LuaScript.var" targets
int lGet(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    Engine& w = self->w();
    float vals[4]{};
    int n = 0;
    std::string err;
    if (lua_isnumber(L, 1)) {
        const entt::entity e = luaReadEnt(L, 1);
        const std::string field = luaL_checkstring(L, 2);
        if (!sceneGetEnt(w.reg, e, field, vals, 4, n, err))
            return luaL_error(L, "scene.get: %s", err.c_str());
    } else {
        const std::string url = luaL_checkstring(L, 1);
        if (!sceneGet(w.reg, url, vals, 4, n, err)) {
            std::string s;
            if (url.find(".LuaScript.") != std::string::npos && self->varStr(w.reg, url, s, err)) {
                lua_pushstring(L, s.c_str());
                return 1;
            }
            return luaL_error(L, "scene.get: %s", err.c_str());
        }
    }
    if (n == 1) {
        lua_pushnumber(L, vals[0]);
        return 1;
    }
    if (n == 4) {
        luaPushV4(L, {vals[0], vals[1], vals[2], vals[3]});
        return 1;
    }
    luaPushV3(L, {vals[0], vals[1], vals[2]});
    return 1;
}

/// scene.scenes() -> table | list loadable scenes in the active scenes dir as an array of {label, path} (label from the json "name" field when present)
int lScenes(lua_State* L) {
    const std::string dir = scenesDir();
    LuaTable arr(L);
    if (std::filesystem::exists(dir))
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            const auto f = e.path().filename().string();
            if (f.size() < 5 || f.substr(f.size() - 5) != ".json")
                continue;
            std::string label = e.path().stem().string();
            try {
                std::ifstream in(e.path().string());
                nlohmann::json j;
                if (in && in >> j)
                    label = j.value("name", label);
            } catch (const std::exception&) {
            }
            LuaTable t(L);
            t.setStr("label", label);
            t.setStr("path", scenesDir() + "/" + f);
            arr.appendTable(t.idx);
            lua_pop(L, 1);
        }
    return 1;
}

/// scene.request_scene(path: string) | queue a scene to load at the end of this frame (e.g. from a UI button)
int lRequestScene(lua_State* L) {
    SceneScript::sSelf(L)->w().pendingScene = luaL_checkstring(L, 1);
    return 0;
}

/// scene.last_error() -> string | error message from the last failed scene load (empty when none)
int lLastError(lua_State* L) {
    lua_pushstring(L, SceneScript::sSelf(L)->w().loadError.c_str());
    return 1;
}

/// scene.time() -> number | seconds since engine start (monotonic, for _animate timing)
int lTime(lua_State* L) {
    lua_pushnumber(L, SceneScript::sSelf(L)->time());
    return 1;
}

const char* kindName(FieldKind k) {
    switch (k) {
    case FieldKind::SliderF: return "slider_f";
    case FieldKind::SliderI: return "slider_i";
    case FieldKind::Checkbox: return "checkbox";
    case FieldKind::Color: return "color";
    case FieldKind::DragV3: return "drag_v3";
    case FieldKind::Ref: return "ref";
    case FieldKind::Str: return "str";
    case FieldKind::StrArr: return "str_arr";
    case FieldKind::Quat: return "quat";
    case FieldKind::FloatArr: return "float_arr";
    case FieldKind::UIntArr: return "uint_arr";
    case FieldKind::EntArr: return "ent_arr";
    }
    return "?";
}

/// scene.fields(type: string) -> array of {name, kind, lo, hi} | reflected field metadata of component type (widget defaults + introspection)
int lFields(lua_State* L) {
    const std::string type = luaL_checkstring(L, 1);
    const CompReg* c = compFor(type);
    if (!c || !c->desc) {
        lua_newtable(L); // unknown type -> empty array (introspection-friendly)
        return 1;
    }
    LuaTable arr(L);
    for (const auto& f : c->desc->fields) {
        LuaTable t(L);
        t.setStr("name", f.name);
        t.setStr("kind", kindName(f.kind));
        t.setNum("lo", f.lo);
        t.setNum("hi", f.hi);
        arr.appendTable(t.idx);
        lua_pop(L, 1);
    }
    lua_pushvalue(L, arr.idx);
    return 1;
}

// push CompFields as a plain Lua table (the shape scene.component snapshots used to have)
static void pushFieldsTable(lua_State* L, const CompFields& fields) {
    lua_newtable(L);
    for (const auto& f : fields) {
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
        lua_setfield(L, -2, f.name.c_str());
    }
}

/// graphics.gltf(path: string) -> table | parse a glTF file as a name->node map (node = component-name -> field table);
/// no world entities are created; pass to scene.spawn to materialize. Compat: graphics.gltf(e, path) still loads
/// straight into e
int lGltf(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    if (lua_isnumber(L, 1)) {
        const entt::entity e = luaReadEnt(L, 1);
        const std::string path = luaL_checkstring(L, 2);
        std::string err;
        if (loadGltf(w.reg, e, path, err) == entt::null)
            return luaL_error(L, "graphics.gltf: %s", err.c_str());
        luaPushEnt(L, e);
        return 1;
    }
    const std::string path = luaL_checkstring(L, 1);
    GltfTableData data;
    std::string err;
    if (!gltfToData(path, data, err))
        return luaL_error(L, "graphics.gltf: %s", err.c_str());
    LuaTable root(L, 0, (int)data.keys.size());
    for (const std::string& key : data.keys) {
        const auto& node = data.nodes[key];
        LuaTable nt(L, 0, (int)node.size());
        for (const auto& [cn, fields] : node) {
            pushFieldsTable(L, fields);
            lua_setfield(L, nt.idx, cn.c_str());
        }
        root.setTable(key.c_str(), nt.idx);
        lua_pop(L, 1);
    }
    return 1;
}

/// graphics.load_texture(path: string, name?: string) -> entity | create a path-only Texture (file loaded and uploaded on first use), optionally named
int lLoadTexture(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const std::string path = luaL_checkstring(L, 1);
    const entt::entity e = w.reg.create();
    if (const char* name = luaL_optstring(L, 2, nullptr))
        w.reg.emplace<Name>(e, name);
    w.reg.emplace<Texture>(e).path = path;
    luaPushEnt(L, e);
    return 1;
}

/// graphics.render_texture(w: int, h: int, name?: string) -> entity | create a standalone GPUTexture render target (no Texture parent); render into it, then attach to a material or environment (errors when the graphics server is disabled)
int lRenderTexture(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    if (!w.hasDevice)
        return luaL_error(L, "graphics.render_texture: graphics server disabled");
    const uint32_t wpx = (uint32_t)luaL_checkinteger(L, 1);
    const uint32_t hpx = (uint32_t)luaL_checkinteger(L, 2);
    const entt::entity e = w.reg.create();
    if (const char* name = luaL_optstring(L, 3, nullptr))
        w.reg.emplace<Name>(e, name);
    w.reg.emplace<GPUTexture>(e, GPUTexture{wpx, hpx});
    w.graphics.makeGpuTexture(e, wpx, hpx);
    luaPushEnt(L, e);
    return 1;
}

/// soft.set_floor(y: number) | set the Y of the soft-body (cloth) floor plane (errors when the graphics server is disabled)
int lSoftFloor(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    if (!w.hasDevice)
        return luaL_error(L, "soft.set_floor: graphics server disabled");
    w.graphics.setFloor((float)luaL_checknumber(L, 1));
    return 0;
}

/// physics.raycast(origin: vec3, dir: vec3, maxDist: number, excludes?: table) -> table | rigid-body raycast; returns {hit = true, pos = vec3, dist = number, entity = entity} or nil when nothing is hit; `excludes` is an array of entities to skip
int lRaycast(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const V3 origin = luaReadV3(L, 1);
    const V3 dir = luaReadV3(L, 2);
    const float maxDist = (float)luaL_checknumber(L, 3);
    std::vector<entt::entity> excludes;
    if (lua_istable(L, 4))
        for (int i = 1;; ++i) {
            lua_geti(L, 4, i);
            if (lua_isnoneornil(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            excludes.push_back(luaReadEnt(L, -1));
            lua_pop(L, 1);
        }
    const RayHit hit = w.physics.raycast(origin, dir, maxDist, excludes);
    if (!hit.hit) {
        lua_pushnil(L);
        return 1;
    }
    const entt::entity bodyEnt = w.physics.entityFor(hit.body);
    LuaTable t(L);
    t.setBool("hit", true);
    luaPushV3(L, hit.pos);
    lua_setfield(L, t.idx, "pos");
    t.setNum("dist", glm::length(hit.pos - origin));
    if (bodyEnt != entt::null) {
        luaPushEnt(L, bodyEnt);
        lua_setfield(L, t.idx, "entity");
    }
    return 1;
}

/// physics.convex_decompose(mesh: entity|Geometry-table, maxHulls?: number, maxVertsPerHull?: number, threshold?:
/// number) -> table | decompose a mesh into convex parts (CoACD); `mesh` is an entity with Geometry or a raw Geometry
/// field table; returns an array of hulls, each a flat {x,y,z,...} point list ready for ConvexHullShape.points; falls
/// back to one hull of all vertices on degenerate input
int lConvexDecompose(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    Mesh mesh;
    if (lua_isnumber(L, 1)) {
        const entt::entity e = luaReadEnt(L, 1);
        if (!w.reg.try_get<Geometry>(e))
            return luaL_error(L, "physics.convex_decompose: no such mesh");
        mesh = w.reg.get<Geometry>(e).mesh;
    } else {
        CompFields fields;
        std::string err;
        if (!readCompFields(L, 1, fields, err, nullptr))
            return luaL_error(L, "physics.convex_decompose: %s", err.c_str());
        for (const auto& f : fields) {
            if (!f.isArr || f.arr.empty())
                continue;
            if (f.name == "pos")
                mesh.pos = f.arr;
            else if (f.name == "nrm")
                mesh.nrm = f.arr;
            else if (f.name == "uv")
                mesh.uv = f.arr;
            else if (f.name == "idx")
                for (float v : f.arr)
                    mesh.indices.push_back((uint32_t)v);
        }
    }
    DecompParams p;
    if (!lua_isnoneornil(L, 2))
        p.maxHulls = (int)luaL_checknumber(L, 2);
    if (!lua_isnoneornil(L, 3))
        p.maxVertsPerHull = (int)luaL_checknumber(L, 3);
    if (!lua_isnoneornil(L, 4))
        p.threshold = (float)luaL_checknumber(L, 4);
    const auto hulls = convexDecompose(mesh, p);
    LuaTable arr(L, (int)hulls.size(), 0);
    for (const auto& h : hulls) {
        LuaTable ht(L, (int)h.size(), 0);
        for (float v : h)
            ht.appendNum(v);
        arr.appendTable(ht.idx);
        lua_pop(L, 1);
    }
    return 1;
}

/// physics.collision_exception(a: entity, b: entity, on?: bool) | toggle the collision exclusion between two bodies:
/// on = true (default) = ignore collisions between them, false = restore; effective from the next physics step
int lCollisionException(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity a = luaReadEnt(L, 1);
    const entt::entity b = luaReadEnt(L, 2);
    bool on = true;
    if (!lua_isnoneornil(L, 3))
        on = lua_toboolean(L, 3);
    w.physics.scene().setExcludes(a, b, on);
    return 0;
}

}

void luaRegisterScene(lua_State* L, SceneScript* host) {
    reg(L, host, "set", lSet);
    reg(L, host, "get", lGet);
    reg(L, host, "time", lTime);
    reg(L, host, "fields", lFields);
    reg(L, host, "scenes", lScenes);
    reg(L, host, "request_scene", lRequestScene);
    reg(L, host, "last_error", lLastError);
}

void luaRegisterGraphicsTable(lua_State* L, SceneScript* host) {
    reg(L, host, "gltf", lGltf);
    reg(L, host, "load_texture", lLoadTexture);
    reg(L, host, "render_texture", lRenderTexture);
}

void luaRegisterSoftTable(lua_State* L, SceneScript* host) {
    reg(L, host, "set_floor", lSoftFloor);
}

void luaRegisterPhysicsTable(lua_State* L, SceneScript* host) {
    reg(L, host, "raycast", lRaycast);
    reg(L, host, "convex_decompose", lConvexDecompose);
    reg(L, host, "collision_exception", lCollisionException);
}
