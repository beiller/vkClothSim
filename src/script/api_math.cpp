#include "script/scene_script.hpp"
#include "script/lua_util.hpp"
#include <glm/ext/quaternion_trigonometric.hpp>

// pure vector/quaternion math (the scene.* vadd..qfromto functions)
void luaRegisterMath(lua_State* L, SceneScript* host) {
    const auto reg = [L, host](const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(L, host);
        lua_pushcclosure(L, fn, 1);
        lua_setfield(L, -2, name);
    };
    reg("vadd", [](lua_State* L) {
        luaPushV3(L, luaReadV3(L, 1) + luaReadV3(L, 2));
        return 1;
    });
    reg("vsub", [](lua_State* L) {
        luaPushV3(L, luaReadV3(L, 1) - luaReadV3(L, 2));
        return 1;
    });
    reg("vscale", [](lua_State* L) {
        luaPushV3(L, luaReadV3(L, 1) * (float)luaL_checknumber(L, 2));
        return 1;
    });
    reg("vlen", [](lua_State* L) {
        lua_pushnumber(L, glm::length(luaReadV3(L, 1)));
        return 1;
    });
    reg("vnorm", [](lua_State* L) {
        luaPushV3(L, glm::normalize(luaReadV3(L, 1)));
        return 1;
    });
    reg("vcross", [](lua_State* L) {
        luaPushV3(L, glm::cross(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
    reg("vdot", [](lua_State* L) {
        lua_pushnumber(L, glm::dot(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
    reg("qmul", [](lua_State* L) {
        luaPushV4(L, v4From(quatFrom(luaReadQuat(L, 1)) * quatFrom(luaReadQuat(L, 2))));
        return 1;
    });
    reg("qinv", [](lua_State* L) {
        luaPushV4(L, v4From(glm::inverse(quatFrom(luaReadQuat(L, 1)))));
        return 1;
    });
    reg("qaxis", [](lua_State* L) {
        const V3 axis = glm::normalize(luaReadV3(L, 1)); // the axis-angle form expects a unit axis
        luaPushV4(L, v4From(glm::angleAxis((float)luaL_checknumber(L, 2), axis)));
        return 1;
    });
    reg("qrot", [](lua_State* L) {
        luaPushV3(L, quatFrom(luaReadQuat(L, 1)) * luaReadV3(L, 2));
        return 1;
    });
    reg("qfromto", [](lua_State* L) {
        luaPushV4(L, quatFromTo(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
}
