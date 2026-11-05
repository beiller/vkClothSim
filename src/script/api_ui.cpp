#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

#include "app/engine.hpp"
#include "ui.hpp"

#include <cstring>
#include "scene.hpp"
#include "url.hpp"

namespace {

void reg(lua_State* L, SceneScript* host, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

const UrlRef bindRef(Engine& w, lua_State* L, int idx) {
    const char* bind = luaL_checkstring(L, idx);
    UrlRef r;
    std::string err;
    if (!resolveUrl(w.reg, bind, r, err))
        luaL_error(L, "bind '%s': %s", bind, err.c_str());
    return r;
}

/// ui.window(title: string, w?: number, h?: number) -> entity | create a UI canvas window (default 320x200) at a fixed world spot in front of the default view; re-place via the canvas Transform
int lUiWindow(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const float w = (float)luaL_optnumber(L, 2, 0.0), h = (float)luaL_optnumber(L, 3, 0.0);
    const V2 px{w > 0.0f ? w : 320.0f, h > 0.0f ? h : 200.0f};
    luaPushEnt(L, spawnUiWindow(self->w().reg, self->w().uiSeq, luaL_checkstring(L, 1), px, V3{0.0f, 1.5f, 2.8f}));
    return 1;
}

/// ui.tab(win: entity, label: string) -> entity | add a tab page to window win
int lUiTab(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1), UITab{luaL_checkstring(L, 2)}));
    return 1;
}

/// ui.editor(tab: entity) | fill tab with the scene editor (entity tree + reflected field widgets for every selected entity)
int lUiEditor(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    spawnEditorUi(self->w(), luaReadEnt(L, 1));
    return 0;
}

/// ui.button(parent: entity, label: string, fn: function) -> entity | add a button under parent; fn is called when it is clicked
int lUiButton(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const entt::entity parent = luaReadEnt(L, 1);
    const std::string label = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    const int ref = self->newRef(L, 3);
    const std::string action = "ui" + std::to_string(self->nextActionSeq());
    self->addButton(action, ref);
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, parent, UIButton{label, action}));
    return 1;
}

/// ui.text(parent: entity, label: string) -> entity | add a static text line under parent
int lUiText(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1), UIText{luaL_checkstring(L, 2)}));
    return 1;
}

/// ui.section(parent: entity, label: string, collapsible?: bool) -> entity | add a (collapsible by default) labeled section under parent; its children are added to the returned entity
int lUiSection(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const bool collapsible = lua_isnoneornil(L, 3) ? true : lua_toboolean(L, 3);
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1), UISection{luaL_checkstring(L, 2), collapsible}));
    return 1;
}

/// ui.slider_f(parent: entity, label: string, bind: string, lo?: number, hi?: number, fmt?: string) -> entity | add a float slider under parent bound to reflected field `bind` (lo/hi default to the field's own range; fmt defaults to "%.2f")
int lUiSliderF(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    const entt::entity e = spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1),
                                      UISliderF{luaL_checkstring(L, 2), *(float*)r.base,
                                                (float)luaL_optnumber(L, 4, r.field->lo),
                                                (float)luaL_optnumber(L, 5, r.field->hi),
                                                luaL_optstring(L, 6, "%.2f")});
    self->w().reg.emplace<Bind>(e, Bind{luaL_checkstring(L, 3)});
    luaPushEnt(L, e);
    return 1;
}

/// ui.slider_i(parent: entity, label: string, bind: string, lo?: number, hi?: number) -> entity | add an integer slider under parent bound to reflected field `bind` (lo/hi default to the field's own range)
int lUiSliderI(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    const entt::entity e = spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1),
                                      UISliderI{luaL_checkstring(L, 2), *(int*)r.base,
                                                (int)luaL_optnumber(L, 4, r.field->lo),
                                                (int)luaL_optnumber(L, 5, r.field->hi)});
    self->w().reg.emplace<Bind>(e, Bind{luaL_checkstring(L, 3)});
    luaPushEnt(L, e);
    return 1;
}

/// ui.checkbox(parent: entity, label: string, bind: string) -> entity | add a checkbox under parent bound to reflected bool field `bind`
int lUiCheckbox(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    const entt::entity e = spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1),
                                      UICheckbox{luaL_checkstring(L, 2), *(bool*)r.base});
    self->w().reg.emplace<Bind>(e, Bind{luaL_checkstring(L, 3)});
    luaPushEnt(L, e);
    return 1;
}

/// ui.color(parent: entity, label: string, bind: string) -> entity | add a color picker under parent bound to reflected vec3 field `bind` (RGB 0..1)
int lUiColor(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    const entt::entity e = spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1),
                                      UIColorEdit{luaL_checkstring(L, 2)});
    std::memcpy(self->w().reg.get<UIColorEdit>(e).value, r.base, 12);
    self->w().reg.emplace<Bind>(e, Bind{luaL_checkstring(L, 3)});
    luaPushEnt(L, e);
    return 1;
}

/// ui.drag_v3(parent: entity, label: string, bind: string, speed?: number) -> entity | add a 3D drag gizmo under parent bound to reflected vec3 field `bind` (default move speed 0.1)
int lUiDragV3(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    const entt::entity e = spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1),
                                      UIDragV3{luaL_checkstring(L, 2)});
    std::memcpy(self->w().reg.get<UIDragV3>(e).value, r.base, 12);
    self->w().reg.get<UIDragV3>(e).speed = (float)luaL_optnumber(L, 4, 0.1f);
    self->w().reg.emplace<Bind>(e, Bind{luaL_checkstring(L, 3)});
    luaPushEnt(L, e);
    return 1;
}

/// ui.fire_button(name: string) | fire a named button callback registered via scene.button(name, fn)
int lFireButton(lua_State* L) {
    SceneScript::sSelf(L)->button(luaL_checkstring(L, 1));
    return 0;
}

}

void luaRegisterUi(lua_State* L, SceneScript* host) {
    reg(L, host, "window", lUiWindow);
    reg(L, host, "tab", lUiTab);
    reg(L, host, "editor", lUiEditor);
    reg(L, host, "button", lUiButton);
    reg(L, host, "text", lUiText);
    reg(L, host, "section", lUiSection);
    reg(L, host, "slider_f", lUiSliderF);
    reg(L, host, "slider_i", lUiSliderI);
    reg(L, host, "checkbox", lUiCheckbox);
    reg(L, host, "color", lUiColor);
    reg(L, host, "drag_v3", lUiDragV3);
    reg(L, host, "fire_button", lFireButton);
}
