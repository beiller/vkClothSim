#pragma once
#include "math.hpp"
#include "mesh.hpp"
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

struct IcoSphere {
    std::vector<V3> verts;
    std::vector<uint32_t> tris;
};

inline IcoSphere makeIcoSphere(int subdiv) {
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<V3> verts{{-1, t, 0},  {1, t, 0},  {-1, -t, 0}, {1, -t, 0}, {0, -1, t},  {0, 1, t},
                          {0, -1, -t}, {0, 1, -t}, {t, 0, -1},  {t, 0, 1},  {-t, 0, -1}, {-t, 0, 1}};
    for (auto& v : verts)
        v = vScale(v, 1.0f / vLen(v));
    std::vector<uint32_t> tris{0, 11, 5,  0, 5,  1, 0, 1, 7, 0, 7,  10, 0, 10, 11, 1, 5, 9, 5, 11,
                               4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3,  9,  4, 3,  4,  2, 3, 2, 6, 3,
                               6, 8,  3,  8, 9,  4, 9, 5, 2, 4, 11, 6,  2, 10, 8,  6, 7, 9, 8, 1};
    for (int s = 0; s < subdiv; ++s) {
        std::map<uint64_t, uint32_t> mid;
        auto midOf = [&](uint32_t a, uint32_t b) {
            const uint64_t key = (a < b) ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
            auto it = mid.find(key);
            if (it != mid.end())
                return it->second;
            V3 m = vScale(vAdd(verts[a], verts[b]), 0.5f);
            m = vScale(m, 1.0f / vLen(m));
            const uint32_t idx = (uint32_t)verts.size();
            verts.push_back(m);
            mid[key] = idx;
            return idx;
        };
        std::vector<uint32_t> next;
        next.reserve(tris.size() * 4);
        for (size_t f = 0; f < tris.size(); f += 3) {
            const uint32_t a = tris[f], b = tris[f + 1], c = tris[f + 2];
            const uint32_t ab = midOf(a, b), bc = midOf(b, c), ca = midOf(c, a);
            for (uint32_t v : {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca})
                next.push_back(v);
        }
        tris = std::move(next);
    }
    return {verts, tris};
}

inline Mesh makeSphereMesh(int subdiv, float radius, const V3& center = {0.0f, 0.0f, 0.0f}) {
    IcoSphere ico = makeIcoSphere(subdiv);
    const int n = (int)ico.verts.size();
    Mesh out;
    out.pos.assign((size_t)3 * n, 0.0f);
    out.nrm.assign((size_t)3 * n, 0.0f);
    out.uv.assign((size_t)2 * n, 0.0f);
    out.indices = ico.tris;
    for (int i = 0; i < n; ++i) {
        const V3 d = ico.verts[i];
        vStore(out.pos.data(), i, vAdd(center, vScale(d, radius)));
        vStore(out.nrm.data(), i, d);
        out.uv[2 * i] = 0.5f + std::atan2f(d.z, d.x) / (2.0f * kPi);
        out.uv[2 * i + 1] = 0.5f + std::asinf(d.y) / kPi;
    }
    return out;
}

inline Mesh makeGroundMesh(float span) {
    Mesh m;
    const V3 corners[4] = {{-span, 0.0f, -span}, {span, 0.0f, -span}, {span, 0.0f, span}, {-span, 0.0f, span}};
    const V3 up{0.0f, 1.0f, 0.0f};
    m.pos.assign(3 * 4, 0.0f);
    m.nrm.assign(3 * 4, 0.0f);
    m.uv.assign(2 * 4, 0.0f);
    for (int i = 0; i < 4; ++i) {
        vStore(m.pos.data(), i, corners[i]);
        vStore(m.nrm.data(), i, up);
        m.uv[2 * i] = (corners[i].x + span) / (2.0f * span);
        m.uv[2 * i + 1] = (corners[i].z + span) / (2.0f * span);
    }
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}
