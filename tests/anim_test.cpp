#include "core/anim.hpp"
#include <cmath>
#include <cstdio>

// pure-math checks for the animation sampler: wrap modes, duration, linear/step/cubic
// sampling, quat slerp. Exits non-zero on any failure.
static int fails = 0;

static void check(const char* name, float got, float want, float tol = 1e-4f) {
    if (std::fabs(got - want) > tol) {
        std::printf("FAIL %s: got %.6f want %.6f\n", name, got, want);
        fails = 1;
    }
}

static void bcheck(const char* name, bool cond) {
    if (!cond) {
        std::printf("FAIL %s\n", name);
        fails = 1;
    }
}

int main() {
    // animWrap: once clamps to [0, dur] (including negative t)
    check("wrap once before", animWrap(-5.0f, 2.0f, 0), 0.0f);
    check("wrap once mid", animWrap(1.0f, 2.0f, 0), animWrap(1.0f, 2.0f, 0), 1.0f);
    check("wrap once after", animWrap(9.0f, 2.0f, 0), animWrap(9.0f, 2.0f, 0), 2.0f);
    // loop wraps; negative t wraps forward
    check("wrap loop mid", animWrap(3.5f, 2.0f, 1), animWrap(3.5f, 2.0f, 1), 1.5f);
    check("wrap loop neg", animWrap(-0.5f, 2.0f, 1), animWrap(-0.5f, 2.0f, 1), 1.5f);
    // pingpong: period 2*dur, triangle wave
    check("wrap pp up", animWrap(0.5f, 2.0f, 2), animWrap(0.5f, 2.0f, 2), 0.5f);
    check("wrap pp peak", animWrap(2.0f, 2.0f, 2), animWrap(2.0f, 2.0f, 2), 2.0f);
    check("wrap pp down", animWrap(3.0f, 2.0f, 2), animWrap(3.0f, 2.0f, 2), 1.0f);
    check("wrap pp neg", animWrap(-1.0f, 2.0f, 2), animWrap(-1.0f, 2.0f, 2), 1.0f);
    check("wrap zero dur", animWrap(5.0f, 0.0f, 1), 0.0f);

    // animDuration: max last-keyframe time across tracks
    {
        const std::vector<float> ts{0.0f, 2.0f, 5.0f};
        const std::vector<float> times{0.0f, 1.0f, 0.0f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f};
        check("duration", animDuration(ts, times), animDuration(ts, times), 6.0f);
    }

    // linear sampling: clamp at ends, exact midpoint
    {
        const float times[3] = {0.0f, 1.0f, 2.0f};
        const float values[3] = {0.0f, 10.0f, 30.0f};
        float out = -1.0f;
        animSample(times, values, nullptr, 3, 1, 0, false, 0.5f, &out);
        check("linear mid", out, 5.0f);
        animSample(times, values, nullptr, 3, 1, 0, false, -2.0f, &out);
        check("linear clamp lo", out, 0.0f);
        animSample(times, values, nullptr, 3, 1, 0, false, 99.0f, &out);
        check("linear clamp hi", out, 30.0f);
        animSample(times, values, nullptr, 1, 1, 0, false, 5.0f, &out);
        check("single key", out, 0.0f);
    }

    // step: holds the lower keyframe
    {
        const float times[3] = {0.0f, 1.0f, 2.0f};
        const float values[3] = {1.0f, 2.0f, 3.0f};
        float out = -1.0f;
        animSample(times, values, nullptr, 3, 1, 1, false, 0.999f, &out);
        check("step holds lo", out, 1.0f);
        animSample(times, values, nullptr, 3, 1, 1, false, 1.5f, &out);
        check("step holds mid", out, 2.0f);
    }

    // cubic hermite with tangents (units/second): v0=0 m0=1 v1=1 m1=1 -> h=0.5 gives 0.5
    {
        const float times[2] = {0.0f, 1.0f};
        const float values[2] = {0.0f, 1.0f};
        const float tangents[2] = {1.0f, 1.0f}; // [outT0, inT1]
        float out = -1.0f;
        animSample(times, values, tangents, 2, 1, 2, false, 0.5f, &out);
        check("cubic mid", out, 0.5f);
        // start derivative: value just after t=0 ~ m0 * h
        animSample(times, values, tangents, 2, 1, 2, false, 0.001f, &out);
        check("cubic start slope", out, 0.001f, 1e-5f);
    }

    // vec3 linear (width 3)
    {
        const float times[2] = {0.0f, 1.0f};
        const float values[6] = {0.0f, 0.0f, 0.0f, 2.0f, -4.0f, 8.0f};
        float out[3] = {-9, -9, -9};
        animSample(times, values, nullptr, 2, 3, 0, false, 0.25f, out);
        check("vec3 x", out[0], 0.5f);
        check("vec3 y", out[1], -1.0f);
        check("vec3 z", out[2], 2.0f);
    }

    // quat slerp: identity -> 90deg about Z at f=0.5 gives 45deg; storage (x,y,z,w)
    {
        const float a[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const float b[4] = {0.0f, 0.0f, std::sin(0.25f * static_cast<float>(M_PI)),
                            std::cos(0.25f * static_cast<float>(M_PI))};
        float out[4] = {};
        animQuatSlerp(a, b, 0.5f, out);
        check("slerp z", out[2], std::sin(0.125f * static_cast<float>(M_PI)));
        check("slerp w", out[3], std::cos(0.125f * static_cast<float>(M_PI)));
        const float l = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
        check("slerp normalized", l, 1.0f);
        // antipodal: b = -a must take the short path (same result as b)
        const float na[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const float nb[4] = {0.0f, 0.0f, 0.0f, -1.0f};
        animQuatSlerp(na, nb, 0.5f, out);
        check("slerp antipodal w", std::fabs(out[3]), 1.0f);
    }

    // quat slerp through the sampler (width 4)
    {
        const float times[2] = {0.0f, 1.0f};
        const float values[8] = {
            0, 0, 0, 1, 0, 0, std::sin(0.5f * static_cast<float>(M_PI)), std::cos(0.5f * static_cast<float>(M_PI))};
        float out[4] = {};
        animSample(times, values, nullptr, 2, 4, 0, true, 0.5f, out);
        check("quat sample z", out[2], std::sin(0.25f * static_cast<float>(M_PI)));
    }

    // URL cache: hit path, self-heal on entity death, name-change invalidation, .LuaScript. never cached
    {
        entt::registry reg;
        std::string err;
        auto a = reg.create();
        reg.emplace<Name>(a, Name{"a"});
        reg.emplace<Transform>(a);
        UrlRef u1, u2;
        bcheck("cache first resolve", resolveUrl(reg, "a.Transform.pos", u1, err));
        ((V3*)u1.base)->x = 5.f;
        bcheck("cache re-resolve", resolveUrl(reg, "a.Transform.pos", u2, err));
        bcheck("cache hit same ptr", u1.base == u2.base);
        bcheck("cache write visible", ((V3*)u2.base)->x == 5.f);

        // entity death: stale entry must self-heal (miss -> re-resolve fails, then works for a new same-named entity)
        reg.destroy(a);
        UrlRef u3;
        bcheck("dead entity resolves false", !resolveUrl(reg, "a.Transform.pos", u3, err));
        auto a2 = reg.create();
        setName(reg, a2, "a");
        reg.emplace<Transform>(a2); // zero-initialized: the old entry pointed at storage holding x=5
        UrlRef u4;
        bcheck("recreated resolves true", resolveUrl(reg, "a.Transform.pos", u4, err));
        bcheck("recreated fresh data", u4.base != nullptr && ((V3*)u4.base)->x == 0.f);

        // name change via setName invalidates: old URL dies, new URL works
        UrlRef u5;
        bcheck("rename live before", resolveUrl(reg, "a.Transform.pos", u5, err));
        setName(reg, a2, "b");
        bcheck("old name dead", !resolveUrl(reg, "a.Transform.pos", u5, err));
        bcheck("new name live", resolveUrl(reg, "b.Transform.pos", u5, err));

        // .LuaScript. is never cached: after the map entry is replaced the ref must see the new node
        auto s = reg.create();
        reg.emplace<Name>(s, Name{"s"});
        LuaScript ls;
        ls.inst = 0;
        ls.varsF["x"] = 1.f;
        reg.emplace<LuaScript>(s, ls);
        UrlRef v1, v2;
        bcheck("lua first", resolveUrl(reg, "s.LuaScript.x", v1, err));
        auto& m = reg.get<LuaScript>(s).varsF;
        m.erase("x");
        m["x"] = 99.f; // new node: a stale cached pointer would still point at the old (freed) value
        bcheck("lua second", resolveUrl(reg, "s.LuaScript.x", v2, err));
        bcheck("lua sees new value", v2.base && *(float*)v2.base == 99.f);
        if (v2.base)
            *(float*)v2.base = 7.f;
        bcheck("lua write-through", m["x"] == 7.f);
    }

    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails;
}
