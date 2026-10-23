#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

#include "engine.hpp"
#include <cstring>
#include <map>
#include <ranges>

namespace {

void reg(lua_State* L, SceneScript* host, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

entt::entity entityOrName(const Engine& w, lua_State* L, int idx) {
    if (lua_isnumber(L, idx))
        return luaReadEnt(L, idx);
    return findNamedEntity(w.reg, luaL_checkstring(L, idx));
}

// the nearest ancestor (or self) carrying a Skeleton component
entt::entity skelEntity(const Engine& w, entt::entity e) {
    if (e == entt::null)
        return entt::null;
    auto& reg = w.reg;
    if (reg.try_get<Skeleton>(e))
        return e;
    std::map<unsigned, std::vector<entt::entity>> children;
    for (auto [c, p] : reg.view<Parent>().each())
        children[entt::to_integral(p.e)].push_back(c);
    std::vector<entt::entity> stack{e};
    while (!stack.empty()) {
        const entt::entity cur = stack.back();
        stack.pop_back();
        if (reg.try_get<Skeleton>(cur))
            return cur;
        if (auto it = children.find(entt::to_integral(cur)); it != children.end())
            for (entt::entity c : it->second)
                stack.push_back(c);
    }
    return entt::null;
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
            f.isArr = true;
            for (int i = 1;; ++i) {
                lua_geti(L, t, i);
                if (lua_isnoneornil(L, -1)) {
                    lua_pop(L, 1);
                    break;
                }
                if (!lua_isnumber(L, -1)) {
                    lua_pop(L, 1);
                    err = "field '" + std::string(name) + "' must be an array of numbers";
                    lua_pop(L, 1);
                    return false;
                }
                f.arr.push_back((float)lua_tonumber(L, -1));
                lua_pop(L, 1);
                f.n = i;
            }
            if (f.n == 0) {
                err = "field '" + std::string(name) + "' must be a non-empty array of numbers";
                lua_pop(L, 1);
                return false;
            }
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

int lEntity(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = w.reg.create();
    const char* name = luaL_optstring(L, 1, nullptr);
    if (name)
        w.reg.emplace<Name>(e, name);
    luaPushEnt(L, e);
    return 1;
}

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

int lComponent(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    CompFields out;
    if (!compGet(w.reg, e, type, out)) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    for (const auto& f : out) {
        if (f.isStr) {
            lua_pushstring(L, f.str.c_str());
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
    return 1;
}

int lCompRemove(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    std::string err;
    if (!compRemove(w.reg, e, type, err))
        return luaL_error(L, "scene.remove %s: %s", type.c_str(), err.c_str());
    return 0;
}

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

int lGetPose(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = entityOrName(w, L, 1);
    if (e == entt::null || !w.reg.try_get<Transform>(e))
        return luaL_error(L, "scene.get_pose: no such entity with a Transform");
    const Transform& t = w.reg.get<Transform>(e);
    lua_createtable(L, 2, 0);
    lua_createtable(L, 3, 0);
    lua_pushnumber(L, t.pos.x);
    lua_seti(L, -2, 1);
    lua_pushnumber(L, t.pos.y);
    lua_seti(L, -2, 2);
    lua_pushnumber(L, t.pos.z);
    lua_seti(L, -2, 3);
    lua_seti(L, -2, 1);
    lua_createtable(L, 4, 0);
    lua_pushnumber(L, t.quat.x);
    lua_seti(L, -2, 1);
    lua_pushnumber(L, t.quat.y);
    lua_seti(L, -2, 2);
    lua_pushnumber(L, t.quat.z);
    lua_seti(L, -2, 3);
    lua_pushnumber(L, t.quat.w);
    lua_seti(L, -2, 4);
    lua_seti(L, -2, 2);
    return 1;
}

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

int lJoints(lua_State* L) {
    const Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity skel = skelEntity(w, entityOrName(w, L, 1));
    if (skel == entt::null)
        return luaL_error(L, "scene.joints: not a skinned character");
    const auto& js = w.reg.get<Skeleton>(skel).joints;
    lua_createtable(L, (int)js.size(), 0);
    for (size_t i = 0; i < js.size(); ++i) {
        luaPushEnt(L, js[i]);
        lua_seti(L, -2, (int)i + 1);
    }
    return 1;
}

int lSkeleton(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const std::string name = luaL_checkstring(L, 1);
    if (!lua_istable(L, 2))
        return luaL_error(L, "scene.skeleton: second arg must be a table of bone names");
    Skeleton skel;
    skel.name = name;
    for (int i = 1;; ++i) {
        lua_geti(L, 2, i);
        if (lua_isnoneornil(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 1);
            return luaL_error(L, "scene.skeleton: bone names must be strings");
        }
        skel.boneNames.push_back(lua_tostring(L, -1));
        skel.invBinds.push_back(mat4Identity());
        lua_pop(L, 1);
    }
    const entt::entity e = w.reg.create();
    w.reg.emplace<Name>(e, name);
    w.reg.emplace<Skeleton>(e, std::move(skel));
    luaPushEnt(L, e);
    return 1;
}

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

int lDestroy(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    if (!w.reg.valid(e))
        return 0;
    if (w.reg.try_get<CapsuleShape>(e) || w.reg.try_get<BoxShape>(e) || w.reg.try_get<TaperedCapsule>(e))
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

}

void luaRegisterComponents(lua_State* L, SceneScript* host) {
    reg(L, host, "entity", lEntity);
    reg(L, host, "emplace", lCompEmplace);
    reg(L, host, "replace", lCompReplace);
    reg(L, host, "component", lComponent);
    reg(L, host, "remove", lCompRemove);
    reg(L, host, "set_pose", lSetPose);
    reg(L, host, "get_pose", lGetPose);
    reg(L, host, "set_morph", lSetMorph);
    reg(L, host, "shapes", lShapes);
    reg(L, host, "link_mesh", lLinkMesh);
    reg(L, host, "joints", lJoints);
    reg(L, host, "skeleton", lSkeleton);
    reg(L, host, "find", lFind);
    reg(L, host, "key_down", lKeyDown);
    reg(L, host, "attach_script", lAttachScript);
    reg(L, host, "destroy", lDestroy);
    reg(L, host, "add_child", lAddChild);
}
