#pragma once
#include "components.hpp"
#include "ecs.hpp"
#include "url.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// Animation sampling math (pure, unit-testable) plus the per-clip step used by stepAnimations.
// Track layout: track i owns keyframes [trackStart[i], trackStart[i+1]) of times and the same
// range * field width of values. Per-track interp: 0=linear 1=step 2=cubic. Cubic tangents
// (optional, concatenated per track): 2*(n-1)*width floats laid out [outT0, inT1, outT1, ...].

inline int fieldBytes(FieldKind k) {
    switch (k) {
    case FieldKind::SliderF:
    case FieldKind::SliderI:
        return 4;
    case FieldKind::Checkbox:
        return 1;
    case FieldKind::Color:
    case FieldKind::DragV3:
        return 12;
    case FieldKind::Quat:
        return 16;
    case FieldKind::Ref:
        return 8;
    default:
        break;
    }
    return 0;
}

// once: clamp to [0, dur]; loop: wrap; pingpong: triangle over 2*dur. Valid for negative t.
inline float animWrap(float t, float dur, int loop) {
    if (dur <= 0.0f)
        return 0.0f;
    if (loop == 0)
        return std::min(std::max(t, 0.0f), dur);
    const float per = loop == 2 ? 2.0f * dur : dur;
    const float r = t - std::floor(t / per) * per; // [0, per)
    return loop == 2 && r > dur ? 2.0f * dur - r : r;
}

// max last-keyframe time across tracks (0 when there are none)
inline float animDuration(const std::vector<float>& trackStart, const std::vector<float>& times) {
    float d = 0.0f;
    for (size_t i = 1; i < trackStart.size(); ++i) {
        const size_t s1 = (size_t)trackStart[i];
        if (s1 > (size_t)trackStart[i - 1] && s1 <= times.size())
            d = std::max(d, times[s1 - 1]);
    }
    return d;
}

// lower bracket j with times[j] <= t < times[j+1] and fraction f in [0,1]; clamps outside range
inline void animBracket(const float* times, int n, float t, int& j, float& f) {
    if (n < 2 || t <= times[0]) {
        j = 0;
        f = 0.0f;
        return;
    }
    if (t >= times[n - 1]) {
        j = n - 2;
        f = 1.0f;
        return;
    }
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        const int mid = (lo + hi) >> 1;
        if (times[mid] <= t)
            lo = mid;
        else
            hi = mid;
    }
    j = lo;
    f = (t - times[lo]) / (times[lo + 1] - times[lo]);
}

// shortest-path slerp of two quats in (x,y,z,w) storage; normalizes the result
inline void animQuatSlerp(const float* a, const float* b, float f, float* out) {
    const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    const float s = d < -1e-6f ? -1.0f : 1.0f; // epsilon: rounding can make near-zero dots negative
    if (d > 1.0f - 1e-5f) {                    // near-parallel: linear then normalize
        for (int k = 0; k < 4; ++k)
            out[k] = a[k] + f * s * (s * b[k] - a[k]);
    } else {
        const float th = std::acos(std::min(1.0f, std::max(-1.0f, d)));
        const float is = 1.0f / std::sin(th);
        const float wa = std::sin((1.0f - f) * th) * is;
        const float wb = s * std::sin(f * th) * is;
        for (int k = 0; k < 4; ++k)
            out[k] = a[k] * wa + b[k] * wb;
    }
    const float l = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    if (l > 1e-9f)
        for (int k = 0; k < 4; ++k)
            out[k] /= l;
}

// sample n keyframes at time t into out; width = floats per value (1/3/4); pointers already point at the track's data;
// quat targets slerp
inline void animSample(const float* times, const float* values, const float* tangents, int n, int width, int interp,
                       bool quat, float t, float* out) {
    if (n <= 1 || t <= times[0]) {
        std::memcpy(out, values, (size_t)width * 4);
        return;
    }
    if (t >= times[n - 1]) {
        std::memcpy(out, values + (size_t)(n - 1) * (size_t)width, (size_t)width * 4);
        return;
    }
    int j = 0;
    float f = 0.0f;
    animBracket(times, n, t, j, f);
    const float* v0 = values + (size_t)j * (size_t)width;
    const float* v1 = values + ((size_t)j + 1) * (size_t)width;
    if (interp == 1) { // step: hold the lower keyframe
        std::memcpy(out, v0, (size_t)width * 4);
        return;
    }
    if (interp == 2 && tangents) { // cubic hermite; tangents are in units/second
        const float h = t - times[j];
        const float* m0 = tangents + (size_t)2 * j * (size_t)width;
        const float* m1 = tangents + ((size_t)2 * j + 1) * (size_t)width;
        for (int k = 0; k < width; ++k) {
            const float h2 = h * h, h3 = h2 * h;
            out[k] = (2.0f * h3 - 3.0f * h2 + 1.0f) * v0[k] + (h3 - 2.0f * h2 + h) * m0[k] +
                     (-2.0f * h3 + 3.0f * h2) * v1[k] + (h3 - h2) * m1[k];
        }
        if (quat) {
            const float l = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
            if (l > 1e-9f)
                for (int k = 0; k < 4; ++k)
                    out[k] /= l;
        }
        return;
    }
    if (interp == 2) { // cubic without tangents: linear fallback
        static bool s_cubicWarned = false;
        if (!s_cubicWarned) {
            std::fprintf(stderr, "3dsim: animation track has no cubic tangents (linear fallback)\n");
            s_cubicWarned = true;
        }
    }
    if (quat) {
        animQuatSlerp(v0, v1, f, out);
        return;
    }
    for (int k = 0; k < width; ++k)
        out[k] = v0[k] + f * (v1[k] - v0[k]);
}

// sample all of a clip's tracks at the (already wrapped) time t and write them to their targets.
// Value/tangent segments are concatenated per track; offsets accumulate using each track's own width.
inline void animStepClip(const entt::registry& reg, AnimationClip& c, float t) {
    const size_t nT = c.targets.size();
    struct Trk {
        UrlRef u;
        int wf = 0; // floats per keyframe value (0 = unresolved or not animatable)
        size_t start = 0;
        int n = 0;
    };
    std::vector<Trk> trks(nT);
    for (size_t i = 0; i < nT; ++i) { // resolve every track so all widths are known for offset math
        Trk& k = trks[i];
        if (c.trackStart.size() >= nT + 1) {
            k.start = (size_t)c.trackStart[i];
            k.n = (int)(c.trackStart[i + 1] - c.trackStart[i]);
        } else if (c.trackStart.empty() && nT > 0 && c.times.size() % nT == 0) { // equal keyframe counts
            const int cnt = (int)(c.times.size() / nT);
            k.start = i * (size_t)cnt;
            k.n = cnt;
        } else {
            continue; // malformed trackStart
        }
        if (k.n < 1 || k.start + (size_t)k.n > c.times.size())
            continue;
        std::string err;
        if (!resolveUrl(reg, c.targets[i], k.u, err))
            continue;
        const int wf = fieldBytes(k.u.field->kind) / 4;
        if (wf == 0) // animatable kinds only: float/int (1), vec3 (3), quat (4)
            continue;
        k.wf = wf;
    }
    size_t vOff = 0, tanBase = 0;
    for (size_t i = 0; i < nT; ++i) {
        const Trk& k = trks[i];
        if (k.n < 1)
            continue;
        const int wf = k.wf > 0 ? k.wf : 3; // unresolved width: assume vec3, offsets after this track may drift
        if (k.wf == 0) {
            vOff += (size_t)k.n * (size_t)wf;
            tanBase += (k.n > 1 ? (size_t)2 * (size_t)(k.n - 1) : 0) * (size_t)wf;
            continue;
        }
        if (vOff + (size_t)k.n * (size_t)wf > c.values.size())
            continue; // values segment too short for this field width
        const int interp = i < c.interps.size() ? (int)c.interps[i] : 0;
        const size_t tanSeg = (k.n > 1 ? (size_t)2 * (size_t)(k.n - 1) : 0) * (size_t)wf;
        const float* tangents = nullptr;
        if (!c.tangents.empty() && tanBase + tanSeg <= c.tangents.size())
            tangents = c.tangents.data() + tanBase;
        animSample(c.times.data() + k.start, c.values.data() + vOff, tangents, k.n, wf, interp,
                   k.u.field->kind == FieldKind::Quat, t, (float*)k.u.base);
        vOff += (size_t)k.n * (size_t)wf;
        tanBase += tanSeg;
    }
}
