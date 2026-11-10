#include "script/scene_script.hpp"
#include "script/lua_util.hpp"

#include "app/engine.hpp"

namespace {

void reg(lua_State* L, SceneScript* host, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

/// engine.performance_metrics() -> string | the engine's performance summary (fps + per-phase ms averages), written by the engine each frame
int lPerformanceMetrics(lua_State* L) {
    const std::string& s = SceneScript::sSelf(L)->w().perfText;
    lua_pushstring(L, s.c_str());
    return 1;
}

}

void luaRegisterEngineTable(lua_State* L, SceneScript* host) {
    reg(L, host, "performance_metrics", lPerformanceMetrics);
}
