#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

#include "engine.hpp"
#include "assets.hpp"
#include "gltf.hpp"
#include "url.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {

void reg(lua_State* L, SceneScript* host, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

int lSet(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
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
        if (vals.empty() || !sceneSetEnt(w, e, field, vals.data(), (int)vals.size(), err))
            return luaL_error(L, "scene.set %s: %s", field.c_str(), err.c_str());
        return 0;
    }
    const std::string url = luaL_checkstring(L, 1);
    const auto vals = readVals(2);
    if (vals.empty() || !sceneSet(w, url, vals.data(), (int)vals.size(), err))
        return luaL_error(L, "scene.set '%s': %s", url.c_str(), err.c_str());
    return 0;
}

int lGet(lua_State* L) {
    const Engine& w = SceneScript::sSelf(L)->w();
    float vals[4]{};
    int n = 0;
    std::string err;
    const bool ok = lua_isnumber(L, 1)
                        ? sceneGetEnt(w, luaReadEnt(L, 1), luaL_checkstring(L, 2), vals, 4, n, err)
                        : sceneGet(w, luaL_checkstring(L, 1), vals, 4, n, err);
    if (!ok)
        return luaL_error(L, "scene.get: %s", err.c_str());
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

int lScenes(lua_State* L) {
    const std::string dir = std::filesystem::exists("assets/scenes") ? "assets/scenes"
                                                                  : exeDir() + "/../assets/scenes";
    lua_newtable(L);
    int i = 0;
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
            lua_newtable(L);
            lua_pushstring(L, label.c_str());
            lua_setfield(L, -2, "label");
            lua_pushstring(L, ("assets/scenes/" + f).c_str());
            lua_setfield(L, -2, "path");
            lua_rawseti(L, -2, ++i);
        }
    return 1;
}

int lRequestScene(lua_State* L) {
    SceneScript::sSelf(L)->w().pendingScene = luaL_checkstring(L, 1);
    return 0;
}

int lLastError(lua_State* L) {
    lua_pushstring(L, SceneScript::sSelf(L)->w().loadError.c_str());
    return 1;
}

int lTime(lua_State* L) {
    lua_pushnumber(L, SceneScript::sSelf(L)->time());
    return 1;
}

int lButton(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    self->addButton(name, self->newRef(L, 2));
    return 0;
}

// render.gltf
int lGltf(lua_State* L) {
    Engine& w = SceneScript::sSelf(L)->w();
    const entt::entity e = luaReadEnt(L, 1);
    const std::string path = luaL_checkstring(L, 2);
    std::string err;
    if (loadGltf(w.reg, e, path, err) == entt::null)
        return luaL_error(L, "render.gltf: %s", err.c_str());
    luaPushEnt(L, e);
    return 1;
}

// soft.floor
int lSoftFloor(lua_State* L) {
    SceneScript::sSelf(L)->w().gpuSim.soft.setFloor((float)luaL_checknumber(L, 1));
    return 0;
}

// physics.raycast
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
    const RayHit hit = w.rigid.raycast(origin, dir, maxDist, excludes);
    if (!hit.hit) {
        lua_pushnil(L);
        return 1;
    }
    const entt::entity bodyEnt = w.rigid.entityFor(hit.body);
    lua_newtable(L);
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "hit");
    luaPushV3(L, hit.pos);
    lua_setfield(L, -2, "pos");
    lua_pushnumber(L, maxDist);
    lua_setfield(L, -2, "dist");
    if (bodyEnt != entt::null) {
        luaPushEnt(L, bodyEnt);
        lua_setfield(L, -2, "entity");
    }
    return 1;
}

}

void luaRegisterScene(lua_State* L, SceneScript* host) {
    reg(L, host, "set", lSet);
    reg(L, host, "get", lGet);
    reg(L, host, "time", lTime);
    reg(L, host, "button", lButton);
    reg(L, host, "scenes", lScenes);
    reg(L, host, "request_scene", lRequestScene);
    reg(L, host, "last_error", lLastError);
}

void luaRegisterRenderTable(lua_State* L, SceneScript* host) {
    reg(L, host, "gltf", lGltf);
}

void luaRegisterSoftTable(lua_State* L, SceneScript* host) {
    reg(L, host, "floor", lSoftFloor);
}

void luaRegisterPhysicsTable(lua_State* L, SceneScript* host) {
    reg(L, host, "raycast", lRaycast);
}
