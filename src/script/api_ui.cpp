#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

#include "engine.hpp"
#include "ui.hpp"
#include "scene.hpp"
#include "url.hpp"

namespace {

void reg(lua_State* L, SceneScript* host, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

// resolves a "name.Component.field" bind to a live component address (ui sliders bind into components)
const UrlRef bindRef(Engine& w, lua_State* L, int idx) {
    const char* bind = luaL_checkstring(L, idx);
    UrlRef r;
    std::string err;
    if (!resolveUrl(w.reg, engineRoots(w), bind, r, err))
        luaL_error(L, "bind '%s': %s", bind, err.c_str());
    return r;
}

int lUiWindow(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const float w = (float)luaL_optnumber(L, 2, 0.0), h = (float)luaL_optnumber(L, 3, 0.0);
    const V2 px{w > 0.0f ? w : 320.0f, h > 0.0f ? h : 200.0f};
    // static world spot in front of the default view; scenes may re-place via the canvas Transform
    luaPushEnt(L, spawnUiWindow(self->w().reg, self->w().uiSeq, luaL_checkstring(L, 1), px, V3{0.0f, 1.5f, 2.8f}));
    return 1;
}

int lUiTab(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1), UITab{luaL_checkstring(L, 2)}));
    return 1;
}

int lUiEditor(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    spawnEditorUi(self->w(), luaReadEnt(L, 1));
    return 0;
}

int lUiButton(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const entt::entity parent = luaReadEnt(L, 1);
    const std::string label = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    const int ref = self->newRef(L, 3);
    const std::string action = "ui" + std::to_string(self->nextActionSeq());
    self->addButton(action, ref);
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, parent, UIButton{label, [self, action] { self->button(action); }}));
    return 1;
}

int lUiText(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1), UIText{luaL_checkstring(L, 2)}));
    return 1;
}

int lUiSection(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const bool collapsible = lua_isnoneornil(L, 3) ? true : lua_toboolean(L, 3);
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1), UISection{luaL_checkstring(L, 2), collapsible}));
    return 1;
}

int lUiSliderF(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    auto* p = (float*)r.base;
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1),
                             UISliderF{luaL_checkstring(L, 2), (float)luaL_optnumber(L, 4, r.field->lo),
                                      (float)luaL_optnumber(L, 5, r.field->hi),
                                      luaL_optstring(L, 6, "%.2f"), [p] { return p; }}));
    return 1;
}

int lUiSliderI(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    auto* p = (int*)r.base;
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1),
                             UISliderI{luaL_checkstring(L, 2), (int)luaL_optnumber(L, 4, r.field->lo),
                                      (int)luaL_optnumber(L, 5, r.field->hi), [p] { return p; }}));
    return 1;
}

int lUiCheckbox(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    auto* p = (bool*)r.base;
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1), UICheckbox{luaL_checkstring(L, 2), [p] { return p; }}));
    return 1;
}

int lUiColor(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    auto* p = (float*)r.base;
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1), UIColorEdit{luaL_checkstring(L, 2), [p] { return p; }}));
    return 1;
}

int lUiDragV3(lua_State* L) {
    auto* self = SceneScript::sSelf(L);
    const auto r = bindRef(self->w(), L, 3);
    auto* p = (float*)r.base;
    luaPushEnt(L, spawnWidget(self->w().reg, self->w().uiSeq, luaReadEnt(L, 1),
                             UIDragV3{luaL_checkstring(L, 2), (float)luaL_optnumber(L, 4, 0.1f), [p] { return p; }}));
    return 1;
}

int lFireButton(lua_State* L) {
    SceneScript::sSelf(L)->button(luaL_checkstring(L, 1));
    return 0;
}

}

void luaRegisterUi(lua_State* L, SceneScript* host) {
    reg(L, host, "ui_window", lUiWindow);
    reg(L, host, "ui_tab", lUiTab);
    reg(L, host, "ui_editor", lUiEditor);
    reg(L, host, "ui_button", lUiButton);
    reg(L, host, "ui_text", lUiText);
    reg(L, host, "ui_section", lUiSection);
    reg(L, host, "ui_slider_f", lUiSliderF);
    reg(L, host, "ui_slider_i", lUiSliderI);
    reg(L, host, "ui_checkbox", lUiCheckbox);
    reg(L, host, "ui_color", lUiColor);
    reg(L, host, "ui_drag_v3", lUiDragV3);
    reg(L, host, "fire_button", lFireButton);
}
