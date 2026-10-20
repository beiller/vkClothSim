#include "scene_script.hpp"

#include "assets.hpp"
#include "gltf.hpp"
#include "json.hpp"
#include "ui.hpp"
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

int scenePanic(lua_State* L) {
    luaL_traceback(L, L, lua_tostring(L, -1), 1);
    std::fprintf(stderr, "3dsim: lua panic:\n%s\n", lua_tostring(L, -1));
    std::abort();
    return 0;
}

void pushJson(lua_State* L, const nlohmann::json& j) {
    if (j.is_object()) {
        lua_newtable(L);
        for (const auto& [k, v] : j.items()) {
            pushJson(L, v);
            lua_setfield(L, -2, k.c_str());
        }
    } else if (j.is_array()) {
        lua_newtable(L);
        int i = 0;
        for (const auto& e : j) {
            pushJson(L, e);
            lua_rawseti(L, -2, ++i);
        }
    } else if (j.is_string())
        lua_pushstring(L, j.get<std::string>().c_str());
    else if (j.is_boolean())
        lua_pushboolean(L, j.get<bool>());
    else if (j.is_number())
        lua_pushnumber(L, j.get<double>());
    else
        lua_pushnil(L);
}

const UrlRef bindRef(Engine& w, lua_State* L, int idx) {
    const char* bind = luaL_checkstring(L, idx);
    UrlRef r;
    std::string err;
    if (!resolveUrl(w, bind, r, err))
        luaL_error(L, "bind '%s': %s", bind, err.c_str());
    return r;
}

int uiTraceback(lua_State* L) {
    luaL_traceback(L, L, lua_tostring(L, -1), 0);
    return 1;
}

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

V3 readV3(lua_State* L, int idx, const V3& def = {0, 0, 0}) {
    if (!lua_istable(L, idx))
        return def;
    return {readNum(L, idx, 1, def.x), readNum(L, idx, 2, def.y), readNum(L, idx, 3, def.z)};
}

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

entt::entity entityOrName(const Engine& w, lua_State* L, int idx) {
    if (lua_isnumber(L, idx))
        return readEnt(L, idx);
    return findNamedEntity(w.reg, luaL_checkstring(L, idx));
}

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

}

SceneScript* SceneScript::sSelf(lua_State* L) {
    return (SceneScript*)lua_touserdata(L, lua_upvalueindex(1));
}

void SceneScript::init(Engine& w) {
    m_w = &w;
    L = luaL_newstate();
    lua_atpanic(L, scenePanic);
    luaL_openlibs(L);
    for (const char* lib : {"io", "os", "debug", "package"}) {
        lua_pushnil(L);
        lua_setglobal(L, lib);
    }
    const auto reg = [this](const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, fn, 1);
        lua_setfield(L, -2, name);
    };

    lua_newtable(L);
    reg("entity", lEntity);
    reg("emplace", lCompEmplace);
    reg("replace", lCompReplace);
    reg("component", lComponent);
    reg("remove", lCompRemove);
    reg("set", lSet);
    reg("get", lGet);
    reg("set_pose", lSetPose);
    reg("get_pose", lGetPose);
    reg("set_morph", lSetMorph);
    reg("joints", lJoints);
    reg("time", lTime);
    reg("button", lButton);
    reg("scenes", lScenes);
    reg("request_scene", lRequestScene);
    reg("last_error", lLastError);
    reg("ui_window", lUiWindow);
    reg("ui_tab", lUiTab);
    reg("ui_editor", lUiEditor);
    reg("ui_button", lUiButton);
    reg("ui_text", lUiText);
    reg("ui_section", lUiSection);
    reg("ui_slider_f", lUiSliderF);
    reg("ui_slider_i", lUiSliderI);
    reg("ui_checkbox", lUiCheckbox);
    reg("ui_color", lUiColor);
    reg("ui_drag_v3", lUiDragV3);
    reg("fire_button", lFireButton);
    reg("attach_script", lAttachScript);
    reg("destroy", lDestroy);
    reg("add_child", lAddChild);
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
    reg("find", lFind);
    reg("key_down", lKeyDown);
    lua_setglobal(L, "scene");

    lua_newtable(L);
    reg("gltf", lGltf);
    lua_setglobal(L, "render");

    lua_newtable(L);
    reg("raycast", lRaycast);
    lua_setglobal(L, "physics");

    lua_newtable(L);
    reg("floor", lSoftFloor);
    lua_setglobal(L, "soft");
    m_time = 0.0f;
}

void SceneScript::watch(entt::registry& reg) {
    reg.on_destroy<LuaScript>().connect<&SceneScript::onScriptDestroy>(this);
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

entt::entity SceneScript::callMeshGen(const std::string& gen, const std::vector<std::pair<std::string, double>>& params,
                                      std::string& err) {
    if (m_geomModule < 0) {
        err = "no geometry module (scenes/mesh not loaded)";
        return entt::null;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_geomModule);
    lua_getfield(L, -1, gen.c_str());
    if (!lua_isfunction(L, -1)) {
        err = "no mesh generator '" + gen + "' in geometry module";
        lua_pop(L, 2);
        return entt::null;
    }
    lua_createtable(L, (int)params.size(), 0);
    for (const auto& [k, v] : params) {
        lua_pushnumber(L, v);
        lua_setfield(L, -2, k.c_str());
    }
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        std::fprintf(stderr, "3dsim: mesh %s: %s\n", gen.c_str(), err.c_str());
        lua_pop(L, 2);
        return entt::null;
    }
    Mesh m = readGeom(L, lua_absindex(L, -1), err);
    lua_pop(L, 2);
    if (!err.empty()) {
        std::fprintf(stderr, "3dsim: mesh %s: %s\n", gen.c_str(), err.c_str());
        return entt::null;
    }
    entt::entity ge = w().reg.create();
    w().reg.emplace<Geometry>(ge, Geometry{m});
    return ge;
}

void SceneScript::buildUi(const nlohmann::json& spec) {
    std::string err;
    const int ref = includeModule("scenes/ui", err);
    if (ref < 0)
        throw SceneError(err);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_getfield(L, -1, "main");
    lua_remove(L, -2);
    pushJson(L, spec);
    lua_pushcclosure(L, uiTraceback, 0);
    lua_insert(L, 1);
    if (lua_pcall(L, 1, 0, 1) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        std::fprintf(stderr, "3dsim: buildUi error:\n%s\n", msg ? msg : "?");
        throw SceneError(msg ? msg : "unknown error");
    }
    lua_pop(L, 1);
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
    for (uint32_t i : m.indices)
        if (i >= (uint32_t)n) {
            err = "geometry: index out of range";
            return {};
        }
    return m;
}

void SceneScript::button(const std::string& name) {
    if (name == "demo-window") { w().showDemo = true; return; }
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

int SceneScript::lScenes(lua_State* L) {
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

int SceneScript::lRequestScene(lua_State* L) {
    sSelf(L)->w().pendingScene = luaL_checkstring(L, 1);
    return 0;
}

int SceneScript::lLastError(lua_State* L) {
    lua_pushstring(L, sSelf(L)->w().loadError.c_str());
    return 1;
}

int SceneScript::lUiWindow(lua_State* L) {
    auto* self = sSelf(L);
    const float x = (float)luaL_optnumber(L, 2, 20.0), y = (float)luaL_optnumber(L, 3, 20.0);
    const float w = (float)luaL_optnumber(L, 4, 0.0), h = (float)luaL_optnumber(L, 5, 0.0);
    const bool collapse = lua_isboolean(L, 6) && lua_toboolean(L, 6);
    pushEnt(L, spawnUiWindow(self->w(), UIWindow{luaL_checkstring(L, 1), [x, y] { return V2{x, y}; }, V2{w, h}, 0, collapse}));
    return 1;
}

int SceneScript::lUiTab(lua_State* L) {
    auto* self = sSelf(L);
    pushEnt(L, spawnWidget(self->w(), readEnt(L, 1), UITab{luaL_checkstring(L, 2)}));
    return 1;
}

int SceneScript::lUiEditor(lua_State* L) {
    auto* self = sSelf(L);
    spawnEditorUi(self->w(), readEnt(L, 1));
    return 0;
}

int SceneScript::lUiButton(lua_State* L) {
    auto* self = sSelf(L);
    const entt::entity parent = readEnt(L, 1);
    const std::string label = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    const int ref = self->newRef(L, 3);
    const std::string action = "ui" + std::to_string(self->m_refs.size());
    self->m_buttons[action] = ref;
    pushEnt(L, spawnWidget(self->w(), parent, UIButton{label, [self, action] { self->button(action); }}));
    return 1;
}

int SceneScript::lUiText(lua_State* L) {
    auto* self = sSelf(L);
    pushEnt(L, spawnWidget(self->w(), readEnt(L, 1), UIText{luaL_checkstring(L, 2)}));
    return 1;
}

int SceneScript::lUiSection(lua_State* L) {
    auto* self = sSelf(L);
    const bool collapsible = lua_isnoneornil(L, 3) ? true : lua_toboolean(L, 3);
    pushEnt(L, spawnWidget(self->w(), readEnt(L, 1), UISection{luaL_checkstring(L, 2), collapsible}));
    return 1;
}

int SceneScript::lUiSliderF(lua_State* L) {
    auto* self = sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    auto* p = (float*)r.base;
    pushEnt(L, spawnWidget(self->w(), readEnt(L, 1),
                           UISliderF{luaL_checkstring(L, 2), (float)luaL_optnumber(L, 4, r.field->lo),
                                    (float)luaL_optnumber(L, 5, r.field->hi),
                                    luaL_optstring(L, 6, "%.2f"), [p] { return p; }}));
    return 1;
}

int SceneScript::lUiSliderI(lua_State* L) {
    auto* self = sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    auto* p = (int*)r.base;
    pushEnt(L, spawnWidget(self->w(), readEnt(L, 1),
                           UISliderI{luaL_checkstring(L, 2), (int)luaL_optnumber(L, 4, r.field->lo),
                                    (int)luaL_optnumber(L, 5, r.field->hi), [p] { return p; }}));
    return 1;
}

int SceneScript::lUiCheckbox(lua_State* L) {
    auto* self = sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    auto* p = (bool*)r.base;
    pushEnt(L, spawnWidget(self->w(), readEnt(L, 1), UICheckbox{luaL_checkstring(L, 2), [p] { return p; }}));
    return 1;
}

int SceneScript::lUiColor(lua_State* L) {
    auto* self = sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    auto* p = (float*)r.base;
    pushEnt(L, spawnWidget(self->w(), readEnt(L, 1), UIColorEdit{luaL_checkstring(L, 2), [p] { return p; }}));
    return 1;
}

int SceneScript::lUiDragV3(lua_State* L) {
    auto* self = sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    auto* p = (float*)r.base;
    pushEnt(L, spawnWidget(self->w(), readEnt(L, 1),
                           UIDragV3{luaL_checkstring(L, 2), (float)luaL_optnumber(L, 4, 0.1f), [p] { return p; }}));
    return 1;
}

int SceneScript::lFireButton(lua_State* L) {
    sSelf(L)->button(luaL_checkstring(L, 1));
    return 0;
}

bool SceneScript::callOnReady(int fnRef, int instRef) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef);
    lua_rawgeti(L, LUA_REGISTRYINDEX, instRef);
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
    lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef);
    lua_rawgeti(L, LUA_REGISTRYINDEX, instRef);
    lua_pushnumber(L, dt);
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
                    ls->ready = true;
                continue;
            }
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod);
            lua_newtable(L);
            pushEnt(L, e);
            lua_setfield(L, -2, "entity");
            if (paramsRef > 0) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, paramsRef);
                lua_setfield(L, -2, "params");
            }
            lua_newtable(L);
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod);
            lua_setfield(L, -2, "__index");
            lua_setmetatable(L, -2);
            const int instRef = newRef(L, -1);
            int onReadyRef = -1, animateRef = -1, physicsRef = -1;
            const auto grab = [this, mod](const char* name, int& out) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, mod);
                lua_getfield(L, -1, name);
                if (lua_isfunction(L, -1))
                    out = newRef(L, -1);
                lua_pop(L, 2);
            };
            grab("_on_ready", onReadyRef);
            grab("_animate", animateRef);
            grab("_physics", physicsRef);
            lua_pop(L, 2);
            const bool ok = (onReadyRef <= 0) || callOnReady(onReadyRef, instRef);
            auto* ls = w().reg.try_get<LuaScript>(e);
            if (!ls) {
                dropRef(instRef);
                dropRef(onReadyRef);
                dropRef(animateRef);
                dropRef(physicsRef);
                continue;
            }
            if (!ok) {
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
            dropRef(ls->*hook);
            ls->*hook = -1;
        }
    }
}

void SceneScript::animateAll(float dt) {
    if (!L)
        return;
    m_time += dt;
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
    m_modules.clear();
    m_geomModule = -1;
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
    lua_pop(L, 2);
}

int SceneScript::lGltf(lua_State* L) {
    auto* self = sSelf(L);
    Engine& w = self->w();
    const entt::entity e = readEnt(L, 1);
    const std::string path = luaL_checkstring(L, 2);
    std::string err;
    if (loadGltf(w, e, path, err) == entt::null)
        return luaL_error(L, "render.gltf: %s", err.c_str());
    pushEnt(L, e);
    return 1;
}

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
            if (lua_isinteger(L, -1)) {
                f.hasEnt = true;
                f.ent = readEnt(L, -1);
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

int SceneScript::lEntity(lua_State* L) {
    auto* self = sSelf(L);
    Engine& w = self->w();
    const entt::entity e = w.reg.create();
    const char* name = luaL_optstring(L, 1, nullptr);
    if (name)
        w.reg.emplace<Name>(e, name);
    pushEnt(L, e);
    return 1;
}

int SceneScript::lCompEmplace(lua_State* L) {
    auto* self = sSelf(L);
    Engine& w = self->w();
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
    Engine& w = self->w();
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
    Engine& w = self->w();
    const entt::entity e = readEnt(L, 1);
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
            pushEnt(L, f.ent);
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

int SceneScript::lCompRemove(lua_State* L) {
    auto* self = sSelf(L);
    Engine& w = self->w();
    const entt::entity e = readEnt(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    std::string err;
    if (!compRemove(w.reg, e, type, err))
        return luaL_error(L, "scene.remove %s: %s", type.c_str(), err.c_str());
    return 0;
}

int SceneScript::lSoftFloor(lua_State* L) {
    auto* self = sSelf(L);
    self->w().gpuSim.soft.setFloor((float)luaL_checknumber(L, 1));
    return 0;
}

int SceneScript::lSet(lua_State* L) {
    auto* self = sSelf(L);
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
        const entt::entity e = readEnt(L, 1);
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

int SceneScript::lGet(lua_State* L) {
    auto* self = sSelf(L);
    const Engine& w = self->w();
    float vals[4]{};
    int n = 0;
    std::string err;
    const bool ok = lua_isnumber(L, 1)
                        ? sceneGetEnt(w, readEnt(L, 1), luaL_checkstring(L, 2), vals, 4, n, err)
                        : sceneGet(w, luaL_checkstring(L, 1), vals, 4, n, err);
    if (!ok)
        return luaL_error(L, "scene.get: %s", err.c_str());
    if (n == 1) {
        lua_pushnumber(L, vals[0]);
        return 1;
    }
    if (n == 4) {
        pushV4(L, {vals[0], vals[1], vals[2], vals[3]});
        return 1;
    }
    pushV3(L, {vals[0], vals[1], vals[2]});
    return 1;
}

int SceneScript::lSetPose(lua_State* L) {
    auto* self = sSelf(L);
    Engine& w = self->w();
    const entt::entity e = entityOrName(w, L, 1);
    if (e == entt::null || !w.reg.try_get<Transform>(e))
        return luaL_error(L, "scene.set_pose: no such entity with a Transform");
    Transform& t = w.reg.get<Transform>(e);
    t.pos = readV3(L, 2);
    t.quat = readQuat(L, 3);
    return 0;
}

int SceneScript::lGetPose(lua_State* L) {
    auto* self = sSelf(L);
    Engine& w = self->w();
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

int SceneScript::lSetMorph(lua_State* L) {
    auto* self = sSelf(L);
    Engine& w = self->w();
    const entt::entity skel = skelEntity(w, entityOrName(w, L, 1));
    if (skel == entt::null)
        return luaL_error(L, "scene.set_morph: not a skinned character");
    w.gpuSim.skin.setMorph(w.reg, skel, luaL_checkstring(L, 2), (float)luaL_checknumber(L, 3));
    return 0;
}

int SceneScript::lJoints(lua_State* L) {
    auto* self = sSelf(L);
    const Engine& w = self->w();
    const entt::entity skel = skelEntity(w, entityOrName(w, L, 1));
    if (skel == entt::null)
        return luaL_error(L, "scene.joints: not a skinned character");
    const auto& js = w.reg.get<Skeleton>(skel).joints;
    lua_createtable(L, (int)js.size(), 0);
    for (size_t i = 0; i < js.size(); ++i) {
        pushEnt(L, js[i]);
        lua_seti(L, -2, (int)i + 1);
    }
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
    Engine& w = self->w();
    const entt::entity e = readEnt(L, 1);
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
    Engine& w = self->w();
    const entt::entity e = readEnt(L, 1);
    if (!w.reg.valid(e))
        return 0;
    if (w.reg.try_get<CapsuleShape>(e) || w.reg.try_get<BoxShape>(e))
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

int SceneScript::lAddChild(lua_State* L) {
    auto* self = sSelf(L);
    Engine& w = self->w();
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
        if (!w.reg.try_get<Transform>(child))
            return luaL_error(L, "scene.add_child: keepWorld requires the child to have a Transform");
        const auto local =
            transformCompose(transformInvert(worldTransform(w.reg, parent)), worldTransform(w.reg, child));
        w.reg.get<Transform>(child) = local;
    }
    w.reg.emplace_or_replace<Parent>(child, parent);
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

int SceneScript::lFind(lua_State* L) {
    auto* self = sSelf(L);
    const std::string name = luaL_checkstring(L, 1);
    const entt::entity e = findNamedEntity(self->w().reg, name);
    if (e == entt::null) {
        lua_pushnil(L);
        return 1;
    }
    pushEnt(L, e);
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
    std::vector<entt::entity> excludes;
    if (lua_istable(L, 4))
        for (int i = 1;; ++i) {
            lua_geti(L, 4, i);
            if (lua_isnoneornil(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            excludes.push_back(readEnt(L, -1));
            lua_pop(L, 1);
        }
    const RayHit hit = self->w().rigid.raycast(origin, dir, maxDist, excludes);
    if (!hit.hit) {
        lua_pushnil(L);
        return 1;
    }
    const entt::entity bodyEnt = self->w().rigid.entityFor(hit.body);
    lua_newtable(L);
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "hit");
    pushV3(L, hit.pos);
    lua_setfield(L, -2, "pos");
    lua_pushnumber(L, maxDist);
    lua_setfield(L, -2, "dist");
    if (bodyEnt != entt::null) {
        pushEnt(L, bodyEnt);
        lua_setfield(L, -2, "entity");
    }
    return 1;
}
