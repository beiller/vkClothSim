#include "collision_shape_solver.h"
#include "script/lua_util.hpp"
#include "script/scene_script.hpp"

namespace {

void reg(lua_State* L, SceneScript* host, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

bool optNum(lua_State* L, int tidx, const char* key, float& out) {
    lua_getfield(L, tidx, key);
    const bool has = lua_isnumber(L, -1);
    if (has)
        out = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return has;
}

bool optInt(lua_State* L, int tidx, const char* key, int& out) {
    lua_getfield(L, tidx, key);
    const bool has = lua_isnumber(L, -1);
    if (has)
        out = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return has;
}

css::FitOptions readFitOptions(lua_State* L, int idx) {
    css::FitOptions o;
    if (!lua_istable(L, idx))
        return o;
    float f;
    if (optNum(L, idx, "profile_percentile", f))
        o.profilePercentile = f;
    if (optNum(L, idx, "underfit_weight", f))
        o.underfitWeight = f;
    if (optNum(L, idx, "overfit_weight", f))
        o.overfitWeight = f;
    if (optNum(L, idx, "tolerance", f))
        o.tolerance = f;
    if (optNum(L, idx, "overhang_weight", f))
        o.overhangWeight = f;
    if (optNum(L, idx, "overhang_percentile", f))
        o.overhangPercentile = f;
    if (optNum(L, idx, "sphere_aspect", f))
        o.sphereAspect = f;
    if (optNum(L, idx, "sphere_percentile", f))
        o.spherePercentile = f;
    int i;
    if (optInt(L, idx, "profile_bins", i))
        o.profileBins = i;
    if (optInt(L, idx, "profile_dilate", i))
        o.profileDilate = i;
    if (optInt(L, idx, "max_segments", i))
        o.maxSegments = i;
    if (optInt(L, idx, "max_clusters", i))
        o.maxClusters = i;
    return o;
}

void setNumField(lua_State* L, int tidx, const char* key, double v) {
    lua_pushnumber(L, v);
    lua_setfield(L, tidx, key);
}

void setVec3Field(lua_State* L, int tidx, const char* key, css::Vec3 v) {
    luaPushV3(L, V3{v.x, v.y, v.z});
    lua_setfield(L, tidx, key);
}

// leaves the part table on top
void pushPart(lua_State* L, const css::CapsuleFit& p) {
    lua_createtable(L, 0, 10);
    const int part = lua_gettop(L);
    setVec3Field(L, part, "origin", p.origin);
    setVec3Field(L, part, "axis", p.axis);
    setNumField(L, part, "length", p.length);
    setNumField(L, part, "segments", p.segments);
    setNumField(L, part, "rms_error", p.rmsError);
    setNumField(L, part, "max_error", p.maxError);
    setNumField(L, part, "mean_signed_error", p.meanSignedError);
    setNumField(L, part, "profile_score", p.profileScore);
    setNumField(L, part, "overhang_score", p.overhangScore);
    lua_createtable(L, (int)p.tapers.size(), 0);
    const int tapers = lua_gettop(L);
    for (int i = 0; i < (int)p.tapers.size(); ++i) {
        const css::Taper& s = p.tapers[i];
        lua_createtable(L, 4, 0);
        lua_pushnumber(L, s.s0);
        lua_seti(L, -2, 1);
        lua_pushnumber(L, s.s1);
        lua_seti(L, -2, 2);
        lua_pushnumber(L, s.r0);
        lua_seti(L, -2, 3);
        lua_pushnumber(L, s.r1);
        lua_seti(L, -2, 4);
        lua_rawseti(L, tapers, i + 1);
    }
    lua_setfield(L, part, "tapers");
}

void pushCompoundFit(lua_State* L, const css::CompoundFit& fit) {
    lua_createtable(L, 0, 5);
    const int out = lua_gettop(L);
    lua_pushboolean(L, true);
    lua_setfield(L, out, "ok");
    setNumField(L, out, "underfit", fit.underfit);
    setNumField(L, out, "overhang", fit.overhang);
    setNumField(L, out, "score", fit.score);
    lua_createtable(L, (int)fit.parts.size(), 0);
    const int parts = lua_gettop(L);
    for (int i = 0; i < (int)fit.parts.size(); ++i) {
        pushPart(L, fit.parts[i]);
        lua_rawseti(L, parts, i + 1);
    }
    lua_setfield(L, out, "parts");
}

/// physics.fit_capsule(pos: number[], nrm?: number[], opts?: table) -> table, err | fit a compound of tapered-capsule
/// chains (spheres for blob-like clouds) to a vertex cloud. `pos` is flat xyz (at least 3 vertices); `nrm` is flat xyz
/// outward normals of the same length (positions-only fit when absent). `opts` overrides profile_bins,
/// profile_percentile, profile_dilate, underfit_weight, overfit_weight, tolerance, max_segments, overhang_weight,
/// overhang_percentile, sphere_aspect, sphere_percentile, max_clusters. Returns nil + message when nothing fits.
/// Result: {ok = true, underfit = number, overhang = number, score = number, parts = array of part}, where part =
/// {origin = vec3, axis = vec3 (unit), length = number, segments = int, rms_error, max_error, mean_signed_error,
/// profile_score, overhang_score, tapers = array of {s0, s1, r0, r1}}; a part with length 0 and one taper r0 = r1 is a
/// sphere. Feed the parts straight into TaperedCapsule components (one entity per part).
///     local fit, err = physics.fit_capsule(pos, nrm)
///     if not fit then error(err) end
///     for _, p in ipairs(fit.parts) do print(p.length, p.rms_error) end
int lFitCapsule(lua_State* L) {
    std::string err;
    std::vector<float> pos, nrm;
    if (!luaReadNumArray(L, 1, pos, err))
        return luaL_error(L, "physics.fit_capsule: pos %s", err.c_str());
    if (pos.size() % 3 != 0 || pos.size() < 9)
        return luaL_error(L, "physics.fit_capsule: pos must be xyz-interleaved with at least 3 vertices");
    if (!lua_isnoneornil(L, 2)) {
        if (!luaReadNumArray(L, 2, nrm, err))
            return luaL_error(L, "physics.fit_capsule: nrm %s", err.c_str());
        if (nrm.size() != pos.size())
            return luaL_error(L, "physics.fit_capsule: nrm must have the same length as pos");
    }
    const css::CompoundFit fit = css::FitTaperedCapsuleCompound(pos.data(), nrm.empty() ? nullptr : nrm.data(), pos.size() / 3, readFitOptions(L, 3));
    if (!fit.ok) {
        lua_pushnil(L);
        lua_pushstring(L, "physics.fit_capsule: no shape fit the cloud");
        return 2;
    }
    pushCompoundFit(L, fit);
    return 1;
}

} // namespace

void luaRegisterFitTable(lua_State* L, SceneScript* host) {
    reg(L, host, "fit_capsule", lFitCapsule);
}
