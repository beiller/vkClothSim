#include "app/engine.hpp"
#include "assets.hpp"
#include "gltf.hpp"
#include "physics/mesh_decomp.hpp"
#include "script/lua_util.hpp"
#include "script/scene_script.hpp"
#include "url.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {

void reg(lua_State* L, SceneScript* host, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

/// scene.set(target: entity|string, field: string, value: number|table|string) | set a reflected field. Two forms:
/// (entity, "field", value) writes one component field on an entity, or ("Name.Component.field", value) walks a URL to
/// a scalar/vec3/quat/color field; ("name.LuaScript.var", string) sets a script variable. `value` is a number, an array
/// of up to 4 numbers (pos/quat/color), or a string for LuaScript targets. Errors on unknown names or fields.
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
    if (lua_isstring(L, 2) && url.contains(".LuaScript.")) {
        self->setVarStr(w.reg, url, luaL_checkstring(L, 2), err);
        return 0;
    }
    const auto vals = readVals(2);
    if (vals.empty() || !sceneSet(w.reg, url, vals.data(), (int)vals.size(), err))
        return luaL_error(L, "scene.set '%s': %s", url.c_str(), err.c_str());
    return 0;
}

/// scene.get(target: entity|string, field?: string) -> number|vec3|vec4|string | read a reflected field. (entity,
/// "field") reads one component field; ("Name.Component.field") walks a URL; ("name.LuaScript.var") reads a script
/// variable as a string. Returns a scalar for 1-component fields, {x,y,z} for vec3/color, {x,y,z,w} for quats, or the
/// string itself for LuaScript targets. Errors on unknown names or fields.
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
            if (url.contains(".LuaScript.") && SceneScript::varStr(w.reg, url, s, err)) {
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

/// scene.scenes() -> table | the loadable scenes in the active scenes dir as an array of {label = string, path =
/// string}; label comes from the json "name" field when present, else the file stem. Feed `path` to request_scene.
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
            } catch (const std::exception&) { // NOLINT(bugprone-empty-catch): malformed scene json falls back to the
                                              // dir name
            }
            LuaTable t(L);
            t.setStr("label", label);
            t.setStr("path", scenesDir() + "/" + f);
            arr.appendTable(t.idx);
            lua_pop(L, 1);
        }
    return 1;
}

/// scene.request_scene(path: string) | queue a scene (a `path` from scene.scenes()) to load at the end of this frame -
/// the usual way for a UI button to switch scenes. Check scene.last_error() afterwards if it fails.
int lRequestScene(lua_State* L) {
    SceneScript::sSelf(L)->w().pendingScene = luaL_checkstring(L, 1);
    return 0;
}

/// scene.last_error() -> string | the error message from the last failed scene load, or "" when the last load succeeded
/// (or none was attempted).
int lLastError(lua_State* L) {
    lua_pushstring(L, SceneScript::sSelf(L)->w().loadError.c_str());
    return 1;
}

/// scene.time() -> number | seconds since engine start (monotonic); use it for _animate timing rather than accumulating
/// dt yourself.
int lTime(lua_State* L) {
    lua_pushnumber(L, SceneScript::sSelf(L)->time());
    return 1;
}

const char* kindName(FieldKind k) {
    switch (k) {
    case FieldKind::SliderF:
        return "slider_f";
    case FieldKind::SliderI:
        return "slider_i";
    case FieldKind::Checkbox:
        return "checkbox";
    case FieldKind::Color:
        return "color";
    case FieldKind::DragV3:
        return "drag_v3";
    case FieldKind::Ref:
        return "ref";
    case FieldKind::Str:
        return "str";
    case FieldKind::StrArr:
        return "str_arr";
    case FieldKind::Quat:
        return "quat";
    case FieldKind::FloatArr:
        return "float_arr";
    case FieldKind::UIntArr:
        return "uint_arr";
    case FieldKind::EntArr:
        return "ent_arr";
    }
    return "?";
}

/// scene.fields(type: string) -> table | the reflected field metadata of component type as an array of {name = string,
/// kind = string, lo = number, hi = number}; kind is one of slider_f, slider_i, checkbox, color, drag_v3, ref, str,
/// str_arr, quat, float_arr, uint_arr, ent_arr. Empty table for unknown types.
int lFields(lua_State* L) {
    const std::string type = luaL_checkstring(L, 1);
    const TypeDesc* desc = refl::findType(type.c_str());
    if (!desc) {
        lua_newtable(L);
        return 1;
    }
    LuaTable arr(L);
    for (const auto& f : desc->fields) {
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

void pushFieldsTable(lua_State* L, const CompFields& fields) {
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

/// graphics.gltf(path: string) -> table | parse a glTF file as a name->node map (node = component-name -> field table)
/// without creating world entities - pass the result to scene.spawn to materialize it. Nodes carry Transform, Parent,
/// Geometry, Renderable, Material, Skeleton, AnimationClip and AnimationPlayer components; refs inside are strings that
/// scene.spawn resolves. Compat form: graphics.gltf(e, path) loads straight into entity e and returns e.
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

/// graphics.load_texture(path: string, name?: string) -> entity | create a Texture entity holding `path` (the file is
/// loaded and uploaded to the GPU lazily on first use); optionally register it under `name`. Use as Material.tex/texMR/
/// texNormal/texEmissive.
int lLoadTexture(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const std::string path = luaL_checkstring(L, 1);
    const entt::entity e = w.reg.create();
    if (const char* name = luaL_optstring(L, 2, nullptr))
        setName(w.reg, e, name);
    w.reg.emplace<Texture>(e).path = path;
    luaPushEnt(L, e);
    return 1;
}

/// graphics.render_texture(w: int, h: int, name?: string) -> entity | create a standalone GPUTexture render target of
/// w x h pixels (no Texture parent); render into it, then attach it to a material or environment. Errors when the
/// graphics server is disabled.
int lRenderTexture(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    if (!w.hasDevice)
        return luaL_error(L, "graphics.render_texture: graphics server disabled");
    const auto wpx = (uint32_t)luaL_checkinteger(L, 1);
    const auto hpx = (uint32_t)luaL_checkinteger(L, 2);
    const entt::entity e = w.reg.create();
    if (const char* name = luaL_optstring(L, 3, nullptr))
        setName(w.reg, e, name);
    w.reg.emplace<GPUTexture>(e, GPUTexture{wpx, hpx});
    w.graphics.makeGpuTexture(e, wpx, hpx);
    luaPushEnt(L, e);
    return 1;
}

/// soft.set_floor(y: number) | set the Y height of the cloth/soft-body floor plane; errors when the graphics server is
/// disabled.
int lSoftFloor(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    if (!w.hasDevice)
        return luaL_error(L, "soft.set_floor: graphics server disabled");
    w.graphics.setFloor((float)luaL_checknumber(L, 1));
    return 0;
}

/// physics.raycast(origin: vec3, dir: vec3, maxDist: number, excludes?: table) -> table|nil | rigid-body raycast against
/// the Jolt world. Returns {hit = true, pos = {x,y,z}, dist = number, entity = entity} (entity omitted when the hit
/// body has no owning entity), or nil when nothing is hit within maxDist. `excludes` is an array of entities whose
/// colliders are skipped.
///     local h = physics.raycast({0, 5, 0}, {0, -1, 0}, 10)
///     if h then print("floor at", h.pos[2]) end
int lRaycast(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const V3 origin = luaReadV3(L, 1);
    const V3 dir = luaReadV3(L, 2);
    const auto maxDist = (float)luaL_checknumber(L, 3);
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

/// physics.convex_decompose(mesh: entity|table, maxHulls?: number, maxVertsPerHull?: number, threshold?: number) -> table |
/// decompose a mesh into convex hulls (CoACD). `mesh` is an entity with Geometry or a raw Geometry field table {pos,
/// nrm?, uv?, idx}. Returns an array of hulls, each a flat {x,y,z,...} point list ready for ConvexHullShape.points;
/// falls back to one hull of all vertices on degenerate input.
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

/// physics.collision_exception(a: entity, b: entity, on?: bool) | toggle the collision exclusion between two rigid
/// bodies: on = true (default) makes them ignore each other, false restores normal collision; effective from the next
/// physics step. Typical use: exclude a character's own colliders or jointed parts from each other.
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

/// scene.animate(target, field, keys, opts?) -> entity | keyframe animation built from a generated clip + player.
/// target = named entity, name string, or an array of them (one track per target, shared clock); field =
/// "Component.field"; keys = array of {t = seconds, v = number|{n1..n4}} sorted by t (all keys must carry the same
/// value count 1/3/4); opts = {loop = 0 once, 1 loop (default), 2 pingpong; speed = number (default 1); playing = bool
/// (default true)}. Returns the AnimationPlayer entity - control it with scene.component(player, "AnimationPlayer") or
/// destroy it to stop.
///     local p = scene.animate("cube", "Transform.pos", {
///         { t = 0, v = {0, 0, 0} }, { t = 1, v = {2, 0, 0} }, { t = 2, v = {0, 0, 0} },
///     }, { loop = 1 })
static int lAnimate(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    auto targetName = [&](int idx) -> std::string {
        if (lua_isnumber(L, idx)) {
            const entt::entity e = luaReadEnt(L, idx);
            if (auto* n = w.reg.try_get<Name>(e))
                return n->id;
            return "";
        }
        const char* s = lua_tostring(L, idx);
        return s ? s : "";
    };
    std::vector<std::string> names;
    if (lua_istable(L, 1)) {
        for (int i = 1;; ++i) {
            lua_geti(L, 1, i);
            if (lua_isnoneornil(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            names.push_back(targetName(-1));
            lua_pop(L, 1);
        }
    } else {
        names.push_back(targetName(1));
    }
    for (const auto& n : names)
        if (n.empty())
            return luaL_error(L, "scene.animate: targets must be named entities");
    const std::string field = luaL_checkstring(L, 2);
    if (!field.contains('.'))
        return luaL_error(L, "scene.animate: field must be 'Component.field'");
    luaL_checktype(L, 3, LUA_TTABLE);
    std::vector<std::pair<float, std::vector<float>>> keys;
    int wdt = -1;
    for (int i = 1;; ++i) {
        lua_geti(L, 3, i);
        if (lua_isnoneornil(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        luaL_checktype(L, -1, LUA_TTABLE);
        lua_getfield(L, -1, "t");
        const float t = (float)luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "v");
        std::vector<float> v;
        if (lua_isnumber(L, -1)) {
            v.push_back((float)lua_tonumber(L, -1));
        } else if (lua_istable(L, -1)) {
            for (int j = 1;; ++j) {
                lua_geti(L, -1, j);
                if (lua_isnoneornil(L, -1)) {
                    lua_pop(L, 1);
                    break;
                }
                v.push_back((float)luaL_checknumber(L, -1));
                lua_pop(L, 1);
            }
        } else {
            return luaL_error(L, "scene.animate: key value must be a number or array");
        }
        lua_pop(L, 2); // v + key table
        if (v.empty() || v.size() > 4)
            return luaL_error(L, "scene.animate: key value count must be 1..4");
        if (wdt < 0)
            wdt = (int)v.size();
        else if ((int)v.size() != wdt)
            return luaL_error(L, "scene.animate: all keys must have the same value count");
        keys.emplace_back(t, std::move(v));
    }
    if (keys.empty())
        return luaL_error(L, "scene.animate: need at least one keyframe");
    std::sort(keys.begin(), keys.end());
    int loop = 1;
    float speed = 1.0f;
    bool playing = true;
    if (lua_istable(L, 4)) {
        lua_getfield(L, 4, "loop");
        if (lua_isnumber(L, -1))
            loop = (int)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 4, "speed");
        if (lua_isnumber(L, -1))
            speed = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 4, "playing");
        if (lua_isboolean(L, -1))
            playing = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    const int nKeys = (int)keys.size();
    std::vector<std::string> urls;
    for (const auto& n : names)
        urls.push_back(n + "." + field);
    std::vector<float> times, values, trackStart{0.0f}, interps(names.size(), 0.0f);
    for (const auto& [t, v] : keys) {
        times.push_back(t);
        values.insert(values.end(), v.begin(), v.end());
    }
    trackStart.push_back((float)nKeys);
    entt::entity clipE = w.reg.create();
    CompFields cf;
    {
        CompField f;
        f.name = "targets";
        f.isStrArr = true;
        f.strArr = std::move(urls);
        cf.push_back(std::move(f));
    }
    const char* arrNames[4] = {"interps", "trackStart", "times", "values"};
    const std::vector<float>* arrs[4] = {&interps, &trackStart, &times, &values};
    for (int i = 0; i < 4; ++i) {
        CompField f;
        f.name = arrNames[i];
        f.isArr = true;
        f.arr = *arrs[i];
        cf.push_back(std::move(f));
    }
    std::string err;
    if (!compEmplace(w.reg, clipE, "AnimationClip", cf, err))
        return luaL_error(L, "scene.animate: %s", err.c_str());
    entt::entity playerE = w.reg.create();
    CompFields pf;
    {
        CompField f;
        f.name = "clip";
        f.hasEnt = true;
        f.ent = clipE;
        pf.push_back(std::move(f));
    }
    {
        CompField f;
        f.name = "loop";
        f.v[0] = (float)loop;
        f.n = 1;
        pf.push_back(std::move(f));
    }
    {
        CompField f;
        f.name = "speed";
        f.v[0] = speed;
        f.n = 1;
        pf.push_back(std::move(f));
    }
    {
        CompField f;
        f.name = "playing";
        f.v[0] = playing ? 1.0f : 0.0f;
        f.n = 1;
        pf.push_back(std::move(f));
    }
    if (!compEmplace(w.reg, playerE, "AnimationPlayer", pf, err))
        return luaL_error(L, "scene.animate: %s", err.c_str());
    luaPushEnt(L, playerE);
    return 1;
}

} // namespace

void luaRegisterScene(lua_State* L, SceneScript* host) {
    reg(L, host, "set", lSet);
    reg(L, host, "get", lGet);
    reg(L, host, "time", lTime);
    reg(L, host, "fields", lFields);
    reg(L, host, "scenes", lScenes);
    reg(L, host, "request_scene", lRequestScene);
    reg(L, host, "last_error", lLastError);
    reg(L, host, "animate", lAnimate);
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
