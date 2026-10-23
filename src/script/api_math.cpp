#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

// pure vector/quaternion math (the scene.* vadd..qfromto functions)
void luaRegisterMath(lua_State* L, SceneScript* host) {
    const auto reg = [L, host](const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(L, host);
        lua_pushcclosure(L, fn, 1);
        lua_setfield(L, -2, name);
    };
    reg("vadd", [](lua_State* L) {
        luaPushV3(L, vAdd(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
    reg("vsub", [](lua_State* L) {
        luaPushV3(L, vSub(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
    reg("vscale", [](lua_State* L) {
        luaPushV3(L, vScale(luaReadV3(L, 1), (float)luaL_checknumber(L, 2)));
        return 1;
    });
    reg("vlen", [](lua_State* L) {
        lua_pushnumber(L, vLen(luaReadV3(L, 1)));
        return 1;
    });
    reg("vnorm", [](lua_State* L) {
        luaPushV3(L, vNorm(luaReadV3(L, 1)));
        return 1;
    });
    reg("vcross", [](lua_State* L) {
        luaPushV3(L, vCross(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
    reg("vdot", [](lua_State* L) {
        lua_pushnumber(L, vDot(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
    reg("qmul", [](lua_State* L) {
        luaPushV4(L, quatMul(luaReadQuat(L, 1), luaReadQuat(L, 2)));
        return 1;
    });
    reg("qinv", [](lua_State* L) {
        luaPushV4(L, quatInv(luaReadQuat(L, 1)));
        return 1;
    });
    reg("qaxis", [](lua_State* L) {
        luaPushV4(L, quatAxisAngle(luaReadV3(L, 1), (float)luaL_checknumber(L, 2)));
        return 1;
    });
    reg("qrot", [](lua_State* L) {
        luaPushV3(L, quatRotate(luaReadQuat(L, 1), luaReadV3(L, 2)));
        return 1;
    });
    reg("qfromto", [](lua_State* L) {
        luaPushV4(L, quatFromTo(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
}
