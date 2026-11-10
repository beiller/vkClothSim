#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

#include "app/engine.hpp"
#include "ui.hpp"

#include "scene.hpp"

namespace {

void reg(lua_State* L, SceneScript* host, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

/// ui.editor(tab: entity) | fill tab with the scene editor (entity tree + reflected field widgets for every selected entity)
int lUiEditor(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    spawnEditorUi(self->w(), luaReadEnt(L, 1));
    return 0;
}

/// ui.seq() -> number | the next UI layout order (shared with the C++ spawn functions)
int lUiSeq(lua_State* L) {
    lua_pushnumber(L, SceneScript::sSelf(L)->w().uiSeq++);
    return 1;
}

/// ui.fire_button(url: string) | call the function named by a "name.LuaScript.fn" url (no arg); the headless stand-in for a button click or widget change
int lFireButton(lua_State* L) {
    SceneScript::sSelf(L)->callUrl(luaL_checkstring(L, 1), entt::null);
    return 0;
}

}

void luaRegisterUi(lua_State* L, SceneScript* host) {
    reg(L, host, "editor", lUiEditor);
    reg(L, host, "seq", lUiSeq);
    reg(L, host, "fire_button", lFireButton);
}
