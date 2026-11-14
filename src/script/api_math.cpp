#include "script/scene_script.hpp"
#include "script/lua_util.hpp"
#include <glm/ext/quaternion_trigonometric.hpp>

void luaRegisterMath(lua_State* L, SceneScript* host) {
    const auto reg = [L, host](const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(L, host);
        lua_pushcclosure(L, fn, 1);
        lua_setfield(L, -2, name);
    };
    /// math.vadd(a: vec3, b: vec3) -> vec3 | element-wise vector addition (vec3 = {x,y,z})
    reg("vadd", [](lua_State* L) {
        luaPushV3(L, luaReadV3(L, 1) + luaReadV3(L, 2));
        return 1;
    });
    /// math.vsub(a: vec3, b: vec3) -> vec3 | element-wise vector subtraction
    reg("vsub", [](lua_State* L) {
        luaPushV3(L, luaReadV3(L, 1) - luaReadV3(L, 2));
        return 1;
    });
    /// math.vscale(v: vec3, s: number) -> vec3 | scalar multiplication (each component * s)
    reg("vscale", [](lua_State* L) {
        luaPushV3(L, luaReadV3(L, 1) * (float)luaL_checknumber(L, 2));
        return 1;
    });
    /// math.vlen(v: vec3) -> number | Euclidean length of v (0 for the zero vector)
    reg("vlen", [](lua_State* L) {
        lua_pushnumber(L, glm::length(luaReadV3(L, 1)));
        return 1;
    });
    /// math.vnorm(v: vec3) -> vec3 | unit-length (normalized) copy of v; undefined for the zero vector
    reg("vnorm", [](lua_State* L) {
        luaPushV3(L, glm::normalize(luaReadV3(L, 1)));
        return 1;
    });
    /// math.vcross(a: vec3, b: vec3) -> vec3 | cross product a x b (perpendicular to both; zero when parallel)
    reg("vcross", [](lua_State* L) {
        luaPushV3(L, glm::cross(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
    /// math.vdot(a: vec3, b: vec3) -> number | dot product a . b (|a||b|cos of the angle between them)
    reg("vdot", [](lua_State* L) {
        lua_pushnumber(L, glm::dot(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
    /// math.qmul(a: quat, b: quat) -> quat | quaternion product a * b (quat = {x,y,z,w}); applying the result to a vector
    /// rotates by b first, then a - so qmul(delta, current) prepends delta to current
    reg("qmul", [](lua_State* L) {
        luaPushQuat(L, luaReadQuat(L, 1) * luaReadQuat(L, 2));
        return 1;
    });
    /// math.qinv(q: quat) -> quat | inverse (conjugate) of q; qmul(q, qinv(q)) is the identity rotation
    reg("qinv", [](lua_State* L) {
        luaPushQuat(L, glm::inverse(luaReadQuat(L, 1)));
        return 1;
    });
    /// math.qaxis(axis: vec3, angle: number) -> quat | axis-angle to quaternion (angle in radians; the axis is
    /// normalized internally)
    reg("qaxis", [](lua_State* L) {
        const V3 axis = glm::normalize(luaReadV3(L, 1));
        luaPushQuat(L, glm::angleAxis((float)luaL_checknumber(L, 2), axis));
        return 1;
    });
    /// math.qrot(q: quat, v: vec3) -> vec3 | rotate vector v by quaternion q
    reg("qrot", [](lua_State* L) {
        luaPushV3(L, luaReadQuat(L, 1) * luaReadV3(L, 2));
        return 1;
    });
    /// math.qfromto(a: vec3, b: vec3) -> quat | the shortest rotation mapping direction a onto b (both normalized
    /// internally); handy for aiming one object at another
    reg("qfromto", [](lua_State* L) {
        luaPushQuat(L, Quat(luaReadV3(L, 1), luaReadV3(L, 2)));
        return 1;
    });
}
