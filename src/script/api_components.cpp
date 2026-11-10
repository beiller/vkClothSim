#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

#include "app/engine.hpp"
#include "core/reflect.hpp"
#include <cstring>
#include <map>
#include <ranges>
#include <set>
#include <unordered_map>
#include <vector>

static void reg(lua_State* L, SceneScript* host, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

entt::entity entityOrName(const Engine& w, lua_State* L, int idx) {
    if (lua_isnumber(L, idx))
        return luaReadEnt(L, idx);
    return findNamedEntity(w.reg, luaL_checkstring(L, idx));
}

// every entity in root's subtree (root + descendants) carrying `comp`; when root is null,
// walks from every top-level entity (one that isn't a child of anyone)
std::vector<entt::entity> findComponentEntities(const Engine& w, entt::entity root, const std::string& comp) {
    const auto& reg = w.reg;
    std::vector<entt::entity> out;
    std::map<unsigned, std::vector<entt::entity>> children;
    std::set<unsigned> isChild, queued;
    for (auto [c, p] : reg.view<Parent>().each()) {
        children[entt::to_integral(p.e)].push_back(c);
        isChild.insert(entt::to_integral(c));
    }
    std::vector<entt::entity> stack;
    const auto enqueue = [&](entt::entity e) {
        if (e != entt::null && queued.insert(entt::to_integral(e)).second)
            stack.push_back(e);
    };
    if (root != entt::null) {
        enqueue(root);
    } else {
        for (auto [e, p] : reg.view<Parent>().each())
            if (isChild.find(entt::to_integral(p.e)) == isChild.end())
                enqueue(p.e);
        for (auto [e, n] : reg.view<Name>().each())
            if (isChild.find(entt::to_integral(e)) == isChild.end())
                enqueue(e);
    }
    while (!stack.empty()) {
        const entt::entity cur = stack.back();
        stack.pop_back();
        if (hasComponentByName(reg, cur, comp))
            out.push_back(cur);
        if (const auto it = children.find(entt::to_integral(cur)); it != children.end())
            for (const entt::entity c : it->second)
                enqueue(c);
    }
    return out;
}

entt::entity skelEntity(const Engine& w, entt::entity e) {
    const auto v = findComponentEntities(w, e, "Skeleton");
    return v.empty() ? entt::null : v.front();
}

bool readCompFields(lua_State* L, int idx, CompFields& out, std::string& err, const char* skipField) {
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        const char* name = luaL_checkstring(L, -2);
        if (skipField && std::strcmp(name, skipField) == 0) {
            lua_pop(L, 1);
            continue;
        }
        CompField f;
        f.name = name;
        if (lua_isboolean(L, -1)) {
            f.v[0] = lua_toboolean(L, -1) ? 1.0f : 0.0f;
            f.n = 1;
        } else if (lua_isnumber(L, -1)) {
            f.v[0] = (float)lua_tonumber(L, -1);
            f.n = 1;
            if (lua_isinteger(L, -1)) {
                f.hasEnt = true;
                f.ent = luaReadEnt(L, -1);
            }
        } else if (lua_isstring(L, -1)) {
            f.isStr = true;
            f.str = lua_tostring(L, -1);
        } else if (lua_istable(L, -1)) {
            const int t = lua_absindex(L, -1);
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
                    err = "field '" + std::string(name) + "' must be an array of numbers or strings";
                    lua_pop(L, 1);
                    return false;
                }
                lua_pop(L, 1);
                f.n = i;
            }
            if (f.n == 0) {
                err = "field '" + std::string(name) + "' must be a non-empty array of numbers or strings";
                lua_pop(L, 1);
                return false;
            }
            if (anyNum && !f.strArr.empty()) {
                err = "field '" + std::string(name) + "' mixes numbers and strings";
                lua_pop(L, 1);
                return false;
            }
            f.isArr = anyNum;
            f.isStrArr = !anyNum;
        } else {
            lua_pop(L, 1);
            err = "field '" + std::string(name) + "' must be a number, boolean, string, or array";
            return false;
        }
        out.push_back(f);
        lua_pop(L, 1);
    }
    return true;
}

bool applyGeomShapes(Engine& w, entt::entity e, lua_State* L, std::string& err) {
    lua_getfield(L, 3, "shapes");
    const bool present = lua_istable(L, -1);
    if (!present) {
        lua_pop(L, 1);
        return true;
    }
    std::vector<SubMesh> shapes;
    if (!luaReadShapesTable(L, 3, w.reg.get<Geometry>(e).mesh.vertexCount(), shapes, err)) {
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);
    w.reg.get<Geometry>(e).mesh.shapes = std::move(shapes);
    return true;
}

void setShapeWeight(entt::registry& reg, entt::entity e, const std::string& name, float weight) {
    if (auto* gc = reg.try_get<Geometry>(e))
        for (auto& sh : gc->mesh.shapes)
            if (sh.name == name)
                sh.weight = weight;
}

std::vector<entt::entity> subtreeEntities(entt::registry& reg, entt::entity root) {
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

/// scene.entity(name?: string) -> entity | create a new entity, optionally named
int lEntity(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = w.reg.create();
    const char* name = luaL_optstring(L, 1, nullptr);
    if (name)
        w.reg.emplace<Name>(e, name);
    luaPushEnt(L, e);
    return 1;
}

/// scene.emplace(e: entity, type: string, fields?: table) | create or replace component `type` on e from a field table (a Geometry table may carry a "shapes" array)
int lCompEmplace(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    const bool geom = type == "Geometry";
    CompFields fields;
    std::string err;
    if (lua_istable(L, 3) && !readCompFields(L, 3, fields, err, geom ? "shapes" : nullptr))
        return luaL_error(L, "scene.emplace %s: %s", type.c_str(), err.c_str());
    if (!compEmplace(w.reg, e, type, fields, err))
        return luaL_error(L, "scene.emplace %s: %s", type.c_str(), err.c_str());
    if (geom && !applyGeomShapes(w, e, L, err))
        return luaL_error(L, "scene.emplace %s: %s", type.c_str(), err.c_str());
    return 0;
}

/// scene.replace(e: entity, type: string, fields?: table) | replace an existing component `type` on e from a field table (errors if the component is absent; Geometry may carry "shapes")
int lCompReplace(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    const bool geom = type == "Geometry";
    CompFields fields;
    std::string err;
    if (lua_istable(L, 3) && !readCompFields(L, 3, fields, err, geom ? "shapes" : nullptr))
        return luaL_error(L, "scene.replace %s: %s", type.c_str(), err.c_str());
    if (!compReplace(w.reg, e, type, fields, err))
        return luaL_error(L, "scene.replace %s: %s", type.c_str(), err.c_str());
    if (geom && !applyGeomShapes(w, e, L, err))
        return luaL_error(L, "scene.replace %s: %s", type.c_str(), err.c_str());
    return 0;
}

entt::entity resolveRef(const std::unordered_map<std::string, entt::entity>& batch, const entt::registry& reg,
                        const std::string& name) {
    const auto it = batch.find(name);
    if (it != batch.end())
        return it->second;
    return findNamedEntity(reg, name);
}

/// scene.spawn(tbl: table, parent?: entity|string) -> table | materialize a name->node map into live entities (node = component-name -> field table). Refs: integer as-is; string = same-spawn name first, then world name. Returns the name->entity map.
int lSpawn(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    luaL_checktype(L, 1, LUA_TTABLE);
    const int tbl = lua_absindex(L, 1);
    entt::entity parent = entt::null;
    if (lua_isnumber(L, 2)) {
        parent = luaReadEnt(L, 2);
    } else if (lua_isstring(L, 2)) {
        parent = findNamedEntity(w.reg, lua_tostring(L, 2));
        if (parent == entt::null)
            return luaL_error(L, "scene.spawn: unknown parent '%s'", lua_tostring(L, 2));
    }
    if (parent != entt::null && !w.reg.try_get<Transform>(parent))
        return luaL_error(L, "scene.spawn: parent must have a Transform");

    // pass 1: create + Name; names deduped to world-unique (#N suffix)
    std::vector<std::string> keys;
    std::unordered_map<std::string, entt::entity> batch;
    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        const char* key = luaL_checkstring(L, -2);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return luaL_error(L, "scene.spawn: node '%s' must be a table", key);
        }
        keys.push_back(key);
        std::string wn = key;
        int n = 2;
        while (findNamedEntity(w.reg, wn) != entt::null)
            wn = std::string(key) + "#" + std::to_string(n++);
        const entt::entity e = w.reg.create();
        w.reg.emplace<Name>(e, wn);
        batch[key] = e;
        lua_pop(L, 1);
    }

    // sub-pass: write all Transform + Parent first so a Skeleton's bindSkeleton sees a complete hierarchy
    for (const std::string& key : keys) {
        const entt::entity e = batch[key];
        lua_getfield(L, tbl, key.c_str());
        const int node = lua_absindex(L, -1);
        for (int ci = 0; ci < 2; ++ci) {
            const char* cn = ci == 0 ? "Transform" : "Parent";
            if (lua_getfield(L, node, cn) == 0) {
                lua_pop(L, 1); // nil
                continue;
            }
            const int fidx = lua_absindex(L, -1);
            CompFields fields;
            std::string err;
            if (!readCompFields(L, fidx, fields, err, nullptr))
                return luaL_error(L, "scene.spawn: %s.%s: %s", key.c_str(), cn, err.c_str());
            for (auto& f : fields)
                if (f.isStr && f.name == "e") { // Parent's entity ref
                    const entt::entity r = resolveRef(batch, w.reg, f.str);
                    if (r == entt::null)
                        return luaL_error(L, "scene.spawn: %s.%s: unknown entity '%s'", key.c_str(), cn, f.str.c_str());
                    f.isStr = false;
                    f.hasEnt = true;
                    f.ent = r;
                }
            if (!compEmplace(w.reg, e, cn, fields, err))
                return luaL_error(L, "scene.spawn: %s.%s: %s", key.c_str(), cn, err.c_str());
            lua_pop(L, 1); // component value
        }
        lua_pop(L, 1); // node table
    }

    // pass 2: emplace remaining components
    for (const std::string& key : keys) {
        const entt::entity e = batch[key];
        lua_getfield(L, tbl, key.c_str());
        const int node = lua_absindex(L, -1);
        std::vector<std::string> comps;
        bool hasGeom = false;
        lua_pushnil(L);
        while (lua_next(L, node) != 0) {
            const std::string cn = lua_tostring(L, -2);
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                return luaL_error(L, "scene.spawn: %s.%s must be a table", key.c_str(), cn.c_str());
            }
            if (cn != "Name") {
                comps.push_back(cn);
                hasGeom = hasGeom || cn == "Geometry";
            }
            lua_pop(L, 1);
        }
        for (const std::string& cn : comps) {
            if (cn == "Transform" || cn == "Parent") // written in the sub-pass
                continue;
            lua_getfield(L, node, cn.c_str());
            const int fidx = lua_absindex(L, -1);
            CompFields fields;
            std::string err;
            if (!readCompFields(L, fidx, fields, err, nullptr)) {
                lua_pop(L, 1);
                return luaL_error(L, "scene.spawn: %s.%s: %s", key.c_str(), cn.c_str(), err.c_str());
            }
            const TypeDesc* t = refl::findType(cn.c_str());
            if (t) {
                for (auto& f : fields) {
                    const FieldDesc* d = findField(t, f.name);
                    if (!d)
                        continue;
                    if (f.isStr && d->kind == FieldKind::Ref) {
                        const entt::entity r = resolveRef(batch, w.reg, f.str);
                        if (r == entt::null) {
                            lua_pop(L, 1);
                            return luaL_error(L, "scene.spawn: %s.%s.%s: unknown entity '%s'", key.c_str(), cn.c_str(),
                                              f.name.c_str(), f.str.c_str());
                        }
                        f.isStr = false;
                        f.hasEnt = true;
                        f.ent = r;
                    } else if (f.isStrArr && d->kind == FieldKind::EntArr) {
                        f.arr.clear();
                        for (const std::string& s : f.strArr) {
                            const entt::entity r = resolveRef(batch, w.reg, s);
                            if (r == entt::null) {
                                lua_pop(L, 1);
                                return luaL_error(L, "scene.spawn: %s.%s.%s: unknown entity '%s'", key.c_str(),
                                                  cn.c_str(), f.name.c_str(), s.c_str());
                            }
                            f.arr.push_back((float)r);
                        }
                        f.isStrArr = false;
                        f.isArr = true;
                    }
                }
            }
            if (cn == "Renderable" && hasGeom) {
                bool haveGeomRef = false;
                for (const auto& f : fields)
                    haveGeomRef = haveGeomRef || f.name == "geom";
                if (!haveGeomRef) {
                    CompField gf;
                    gf.name = "geom";
                    gf.hasEnt = true;
                    gf.ent = e;
                    fields.push_back(std::move(gf));
                }
            }
            if (!compEmplace(w.reg, e, cn, fields, err)) {
                lua_pop(L, 1);
                return luaL_error(L, "scene.spawn: %s.%s: %s", key.c_str(), cn.c_str(), err.c_str());
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    if (parent != entt::null)
        for (const std::string& key : keys)
            if (!w.reg.try_get<Parent>(batch[key]))
                w.reg.emplace<Parent>(batch[key], parent);

    lua_createtable(L, 0, (int)keys.size());
    for (const std::string& key : keys) {
        lua_pushstring(L, key.c_str());
        luaPushEnt(L, batch[key]);
        lua_settable(L, -3);
    }
    return 1;
}

/// scene.has(e: entity, type: string) -> bool | whether e has component `type`
int lHas(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    lua_pushboolean(L, hasComponentByName(w.reg, e, type));
    return 1;
}

// Live component view: __index/__newindex re-resolve the component on every access
// (entt's vector storage can be reallocated, so no component data pointer is ever cached).
// The view table carries its context under private keys "\1ent" (int) and "\1reg" (light userdata).
static void compViewCtx(lua_State* L, entt::entity& e, const CompReg*& c) {
    lua_pushstring(L, "\1ent");
    lua_rawget(L, 1);
    e = (entt::entity)(uint32_t)luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    lua_pushstring(L, "\1reg");
    lua_rawget(L, 1);
    c = static_cast<const CompReg*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
}

static int compViewIndex(lua_State* L) {
    entt::entity e; const CompReg* c;
    compViewCtx(L, e, c);
    const char* name = luaL_checkstring(L, 2);
    Engine& w = SceneScript::sSelf(L)->w();
    void* b = c->tryGet(w.reg, e);
    if (!b) { lua_pushnil(L); return 1; }
    const FieldDesc* d = findField(c->desc, name);
    if (!d) { lua_pushnil(L); return 1; }
    const char* src = (const char*)b + d->offset;
    switch (d->kind) {
    case FieldKind::SliderF:
        lua_pushnumber(L, *(const float*)src);
        break;
    case FieldKind::SliderI:
        lua_pushnumber(L, (double)*(const int*)src);
        break;
    case FieldKind::Checkbox:
        lua_pushboolean(L, *(const bool*)src);
        break;
    case FieldKind::Color:
    case FieldKind::DragV3: {
        const float* v = (const float*)src;
        LuaTable t(L, 3, 0);
        t.appendNum(v[0]); t.appendNum(v[1]); t.appendNum(v[2]);
        break;
    }
    case FieldKind::Quat: {
        const float* v = (const float*)src;
        LuaTable t(L, 4, 0);
        t.appendNum(v[0]); t.appendNum(v[1]); t.appendNum(v[2]); t.appendNum(v[3]);
        break;
    }
    case FieldKind::Ref:
        luaPushEnt(L, *(const entt::entity*)src);
        break;
    case FieldKind::Str:
        lua_pushstring(L, (const char*)(*(const std::string*)src).c_str());
        break;
    case FieldKind::StrArr: {
        const auto& a = *(const std::vector<std::string>*)src;
        LuaTable t(L, (int)a.size(), 0);
        for (const auto& s : a) t.appendStr(s);
        break;
    }
    case FieldKind::FloatArr: {
        const auto& a = *(const std::vector<float>*)src;
        LuaTable t(L, (int)a.size(), 0);
        for (float v : a) t.appendNum(v);
        break;
    }
    case FieldKind::UIntArr: {
        const auto& a = *(const std::vector<uint32_t>*)src;
        LuaTable t(L, (int)a.size(), 0);
        for (uint32_t v : a) t.appendNum(v);
        break;
    }
    case FieldKind::EntArr: {
        const auto& a = *(const std::vector<entt::entity>*)src;
        LuaTable t(L, (int)a.size(), 0);
        int i = 1;
        for (entt::entity v : a) { luaPushEnt(L, v); lua_seti(L, t.idx, i++); }
        break;
    }
    }
    return 1;
}

static int compViewNewIndex(lua_State* L) {
    entt::entity e; const CompReg* c;
    compViewCtx(L, e, c);
    Engine& w = SceneScript::sSelf(L)->w();
    if (!w.reg.valid(e))
        return luaL_error(L, "scene view: entity removed");
    const char* name = luaL_checkstring(L, 2);
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
                lua_pop(L, 2);
                return luaL_error(L, "field '%s' must be an array of numbers or strings", name);
            }
            lua_pop(L, 1);
            f.n = i;
        }
        if (f.n == 0)
            return luaL_error(L, "field '%s' must be a non-empty array of numbers or strings", name);
        if (anyNum && !f.strArr.empty())
            return luaL_error(L, "field '%s' mixes numbers and strings", name);
        f.isArr = anyNum;
        f.isStrArr = !anyNum;
    } else {
        return luaL_error(L, "field '%s' must be a number, boolean, string, or array", name);
    }
    std::string err;
    if (!compWriteField(w.reg, e, c->name, f, err))
        return luaL_error(L, "scene view: %s", err.c_str());
    return 0;
}

static void compViewMetaEnsure(lua_State* L, SceneScript* host) {
    const char* key = "3dsim.CompView.meta";
    lua_getfield(L, LUA_REGISTRYINDEX, key);
    if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pop(L, 1);
    luaL_newmetatable(L, "3dsim.CompView");
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, compViewIndex, 1);
    lua_setfield(L, -2, "__index");
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, compViewNewIndex, 1);
    lua_setfield(L, -2, "__newindex");
    lua_setfield(L, LUA_REGISTRYINDEX, key);
}

/// scene.component(e: entity, type: string) -> table|nil | a live view of component `type` on e (nil if absent); reads resolve per access; writes require the component to exist (create via scene.emplace/replace)
int lComponent(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    const CompReg* c = compFor(type);
    if (!c) {
        std::string msg = refl::findType(type.c_str()) ? "component '" + type + "' is engine state, not an entity component" : "unknown component '" + type + "'";
        return luaL_error(L, "scene.component: %s", msg.c_str());
    }
    if (!c->tryGet(w.reg, e)) {
        lua_pushnil(L);
        return 1;
    }
    compViewMetaEnsure(L, SceneScript::sSelf(L));
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)entt::to_integral(e));
    lua_setfield(L, -2, "\1ent");
    lua_pushlightuserdata(L, (void*)c);
    lua_setfield(L, -2, "\1reg");
    lua_getfield(L, LUA_REGISTRYINDEX, "3dsim.CompView.meta");
    lua_setmetatable(L, -2);
    return 1;
}

/// scene.remove(e: entity, type: string) | remove component `type` from e (errors if absent or unknown type)
int lCompRemove(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    std::string err;
    if (!compRemove(w.reg, e, type, err))
        return luaL_error(L, "scene.remove %s: %s", type.c_str(), err.c_str());
    return 0;
}

/// scene.set_pose(e: entity|string, pos: vec3, quat: quat) | set the local Transform of e (errors if e has no Transform)
int lSetPose(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = entityOrName(w, L, 1);
    if (e == entt::null || !w.reg.try_get<Transform>(e))
        return luaL_error(L, "scene.set_pose: no such entity with a Transform");
    Transform& t = w.reg.get<Transform>(e);
    t.pos = luaReadV3(L, 2);
    t.quat = luaReadQuat(L, 3);
    return 0;
}

/// scene.get_pose(e: entity|string) -> table | local Transform of e as {pos = {x,y,z}, quat = {x,y,z,w}} (errors if e has no Transform)
int lGetPose(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = entityOrName(w, L, 1);
    if (e == entt::null || !w.reg.try_get<Transform>(e))
        return luaL_error(L, "scene.get_pose: no such entity with a Transform");
    const Transform& t = w.reg.get<Transform>(e);
    lua_createtable(L, 0, 2);
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, t.pos.x);
    lua_seti(L, -2, 1);
    lua_pushnumber(L, t.pos.y);
    lua_seti(L, -2, 2);
    lua_pushnumber(L, t.pos.z);
    lua_seti(L, -2, 3);
    lua_setfield(L, -2, "pos");
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, t.quat.x);
    lua_seti(L, -2, 1);
    lua_pushnumber(L, t.quat.y);
    lua_seti(L, -2, 2);
    lua_pushnumber(L, t.quat.z);
    lua_seti(L, -2, 3);
    lua_pushnumber(L, t.quat.w);
    lua_seti(L, -2, 4);
    lua_setfield(L, -2, "quat");
    return 1;
}

/// scene.world_pose(e: entity|string) -> table | world-space pose of e as {pos = {x,y,z}, quat = {x,y,z,w}}; live Transform+Parent walk, always current (unlike the per-substep WorldTransform snapshot)
int lWorldPose(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = entityOrName(w, L, 1);
    if (e == entt::null || !w.reg.try_get<Transform>(e))
        return luaL_error(L, "scene.world_pose: no such entity with a Transform");
    const Transform t = worldTransform(w.reg, e);
    lua_createtable(L, 0, 2);
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, t.pos.x);
    lua_seti(L, -2, 1);
    lua_pushnumber(L, t.pos.y);
    lua_seti(L, -2, 2);
    lua_pushnumber(L, t.pos.z);
    lua_seti(L, -2, 3);
    lua_setfield(L, -2, "pos");
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, t.quat.x);
    lua_seti(L, -2, 1);
    lua_pushnumber(L, t.quat.y);
    lua_seti(L, -2, 2);
    lua_pushnumber(L, t.quat.z);
    lua_seti(L, -2, 3);
    lua_pushnumber(L, t.quat.w);
    lua_seti(L, -2, 4);
    lua_setfield(L, -2, "quat");
    return 1;
}

/// scene.set_morph(e: entity|string, name: string, weight: number) | set the blend weight of shape key `name`; on a skinned character (no Geometry) it applies to all linked meshes
int lSetMorph(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = entityOrName(w, L, 1);
    const std::string name = luaL_checkstring(L, 2);
    const float weight = (float)luaL_checknumber(L, 3);
    if (e != entt::null && w.reg.try_get<Geometry>(e)) {
        setShapeWeight(w.reg, e, name, weight);
        return 0;
    }
    const entt::entity skel = skelEntity(w, e);
    if (skel == entt::null)
        return luaL_error(L, "scene.set_morph: not a skinned character");
    for (const entt::entity m : w.reg.get<Skeleton>(skel).meshes)
        setShapeWeight(w.reg, m, name, weight);
    return 0;
}

/// scene.shapes(e: entity|string) -> table | shape keys of mesh e as an array of {name, weight} (errors if e has no Geometry)
int lShapes(lua_State* L) {
    const Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = entityOrName(w, L, 1);
    if (e == entt::null || !w.reg.try_get<Geometry>(e))
        return luaL_error(L, "scene.shapes: not a mesh");
    const auto& shapes = w.reg.get<Geometry>(e).mesh.shapes;
    lua_createtable(L, (int)shapes.size(), 0);
    for (size_t i = 0; i < shapes.size(); ++i) {
        lua_createtable(L, 0, 2);
        lua_pushstring(L, shapes[i].name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushnumber(L, shapes[i].weight);
        lua_setfield(L, -2, "weight");
        lua_seti(L, -2, (int)i + 1);
    }
    return 1;
}

/// scene.link_mesh(skel: entity|string, mesh: entity|string) | attach mesh to skeleton skel's skinned meshes (errors if skel has no Skeleton)
int lLinkMesh(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity skel = entityOrName(w, L, 1);
    const entt::entity mesh = entityOrName(w, L, 2);
    if (skel == entt::null || !w.reg.try_get<Skeleton>(skel))
        return luaL_error(L, "scene.link_mesh: first arg is not a skeleton");
    if (mesh == entt::null)
        return luaL_error(L, "scene.link_mesh: second arg is not an entity");
    w.reg.get<Skeleton>(skel).meshes.push_back(mesh);
    return 0;
}

/// scene.get_entity(name: string) -> entity | look up a named entity (nil if not found)
int lFind(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    const entt::entity e = findNamedEntity(SceneScript::sSelf(L)->w().reg, name);
    if (e == entt::null) {
        lua_pushnil(L);
        return 1;
    }
    luaPushEnt(L, e);
    return 1;
}

/// scene.find_components(component: string, root?: entity) -> table | every entity in root's subtree carrying `component` (all top-level entities when root is omitted), as an array of entities
int lFindComponents(lua_State* L) {
    const Engine& w = SceneScript::sSelf(L)->w();
    const std::string comp = luaL_checkstring(L, 1);
    const entt::entity root = lua_isnumber(L, 2) ? luaReadEnt(L, 2) : entt::null;
    const auto found = findComponentEntities(w, root, comp);
    lua_createtable(L, (int)found.size(), 0);
    for (size_t i = 0; i < found.size(); ++i) {
        luaPushEnt(L, found[i]);
        lua_seti(L, -2, (int)i + 1);
    }
    return 1;
}

/// scene.named() -> array of {name = string, entity = entity} | every named entity, sorted by entity id (the editor's source)
int lNamed(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    std::vector<std::pair<entt::entity, std::string>> named;
    for (auto [e, n] : w.reg.view<Name>().each())
        named.emplace_back(e, n.id);
    std::ranges::sort(named, [](auto& a, auto& b) { return a.first < b.first; });
    LuaTable arr(L, (int)named.size(), 0);
    for (const auto& [e, name] : named) {
        LuaTable t(L);
        t.setStr("name", name);
        luaPushEnt(L, e);
        lua_setfield(L, t.idx, "entity");
        arr.appendTable(t.idx);
        lua_pop(L, 1);
    }
    lua_pushvalue(L, arr.idx);
    return 1;
}

/// scene.components(e: entity) -> array of string | the reflected component type names on e (the editor's field source)
int lComponents(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    const auto comps = refl::entityFields(w.reg, e);
    LuaTable arr(L, (int)comps.size(), 0);
    for (const auto& [t, ref] : comps)
        if (t)
            arr.appendStr(t->name);
    lua_pushvalue(L, arr.idx);
    return 1;
}

/// input.key_down(kb: entity, code: number) -> bool | true if key `code` is held on the KeyboardInput entity kb (false for out-of-range codes)
int lKeyDown(lua_State* L) {
    const Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    const int code = (int)luaL_checknumber(L, 2);
    if (code < 0 || code >= KeyboardInput::kMaxKeys) {
        lua_pushboolean(L, false);
        return 1;
    }
    const auto* ki = w.reg.try_get<KeyboardInput>(e);
    lua_pushboolean(L, ki && ki->keys[code]);
    return 1;
}

/// scene.attach_script(e: entity, file: string, params?: table) | attach (or re-attach, replacing any existing script) a Lua module to e; `params` is passed to its _on_ready
int lAttachScript(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    Engine& w = self->w();
    const entt::entity e = luaReadEnt(L, 1);
    const std::string file = luaL_checkstring(L, 2);
    if (!w.reg.valid(e))
        return luaL_error(L, "scene.attach_script: invalid entity");
    auto* p = w.reg.try_get<LuaScript>(e);
    LuaScript& ls = p ? *p : w.reg.emplace<LuaScript>(e);
    if (ls.ready) {
        self->destroyScript(ls.inst, ls.params, ls.onReady, ls.animate, ls.physics);
        ls = LuaScript{};
    }
    ls.file = file;
    if (lua_istable(L, 3))
        ls.params = self->newRef(L, 3);
    return 0;
}

/// scene.destroy(e: entity) | destroy e and its whole subtree; errors if e is a shape referenced by a collider, a texture referenced by a material, or a geometry referenced by a renderable outside the subtree
int lDestroy(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    if (!w.reg.valid(e))
        return 0;
    if (w.reg.try_get<CapsuleShape>(e) || w.reg.try_get<BoxShape>(e) || w.reg.try_get<TaperedCapsule>(e) ||
        w.reg.try_get<SphereShape>(e))
        for (auto [c, col] : w.reg.view<Collider>().each())
            if (col.shape == e)
                return luaL_error(L, "scene.destroy: shape is referenced by a collider");
    if (w.reg.try_get<Texture>(e))
        for (auto [m, mat] : w.reg.view<Material>().each())
            if (mat.tex == e || mat.texMR == e || mat.texNormal == e || mat.texEmissive == e)
                return luaL_error(L, "scene.destroy: texture is referenced by a material");
    const auto subtree = subtreeEntities(w.reg, e);
    if (w.reg.try_get<Geometry>(e))
        for (auto [r, rend] : w.reg.view<Renderable>().each())
            if (rend.geom == e && std::ranges::find(subtree, r) == subtree.end())
                return luaL_error(L, "scene.destroy: geometry is referenced by a renderable");
    for (const auto c : subtree)
        w.reg.destroy(c);
    return 0;
}

/// scene.add_child(parent: entity, child: entity, opts?: table) | reparent child under parent (parent needs a Transform); opts.keepWorld = true re-expresses the child's local pose so its world position is unchanged
int lAddChild(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity parent = luaReadEnt(L, 1);
    const entt::entity child = luaReadEnt(L, 2);
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
        if (!w.reg.try_get<Transform>(child))
            return luaL_error(L, "scene.add_child: keepWorld requires the child to have a Transform");
        const auto local =
            transformCompose(transformInvert(worldTransform(w.reg, parent)), worldTransform(w.reg, child));
        w.reg.get<Transform>(child) = local;
    }
    w.reg.emplace_or_replace<Parent>(child, parent);
    return 0;
}

void luaRegisterComponents(lua_State* L, SceneScript* host) {
    reg(L, host, "entity", lEntity);
    reg(L, host, "emplace", lCompEmplace);
    reg(L, host, "replace", lCompReplace);
    reg(L, host, "has", lHas);
    reg(L, host, "component", lComponent);
    reg(L, host, "remove", lCompRemove);
    reg(L, host, "set_pose", lSetPose);
    reg(L, host, "get_pose", lGetPose);
    reg(L, host, "world_pose", lWorldPose);
    reg(L, host, "set_morph", lSetMorph);
    reg(L, host, "shapes", lShapes);
    reg(L, host, "link_mesh", lLinkMesh);
    reg(L, host, "spawn", lSpawn);
    reg(L, host, "get_entity", lFind);
    reg(L, host, "named", lNamed);
    reg(L, host, "components", lComponents);
    reg(L, host, "find_components", lFindComponents);
    reg(L, host, "attach_script", lAttachScript);
    reg(L, host, "destroy", lDestroy);
    reg(L, host, "add_child", lAddChild);
}

void luaRegisterInputTable(lua_State* L, SceneScript* host) {
    reg(L, host, "key_down", lKeyDown);
}
