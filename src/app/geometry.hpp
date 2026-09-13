#pragma once
#include "mesh.hpp"
#include "sim/sim.hpp"
#include <map>

struct SoftMesh {
    Mesh mesh;
    std::vector<sim::Constraint> cons;
};

inline SoftMesh makeCloth(int CW, int CH, float span, float y0) {
    SoftMesh out;
    const int CN = CW * CH;
    out.mesh.pos.assign((size_t)3 * CN, 0.0f);
    out.mesh.nrm.assign((size_t)3 * CN, 0.0f);
    out.mesh.col.assign((size_t)3 * CN, 0.0f);
    for (int gy = 0; gy < CH; ++gy)
        for (int gx = 0; gx < CW; ++gx) {
            int i = gy * CW + gx;
            size_t o = (size_t)3 * i;
            out.mesh.pos[o] = -span / 2 + span * (float)gx / (float)(CW - 1);
            out.mesh.pos[o + 1] = y0;
            out.mesh.pos[o + 2] = -span / 2 + span * (float)gy / (float)(CH - 1);
            out.mesh.nrm[o + 1] = 1.0f;
            out.mesh.col[o] = 0.25f;
            out.mesh.col[o + 1] = 0.45f;
            out.mesh.col[o + 2] = 0.78f;
        }
    const float spacing = span / (float)(CW - 1);
    const float diag = spacing * kSqrt2;
    for (int i = 0; i < CN; ++i) {
        int gx = i % CW, gy = i / CW;
        if (gx < CW - 1)
            out.cons.push_back({i, i + 1, spacing, 1.0f});
        if (gy < CH - 1)
            out.cons.push_back({i, i + CW, spacing, 1.0f});
        if (gx < CW - 1 && gy < CH - 1)
            out.cons.push_back({i, i + CW + 1, diag, 1.0f});
        if (gx > 0 && gy < CH - 1)
            out.cons.push_back({i, i + CW - 1, diag, 1.0f});
    }
    for (int gy = 0; gy < CH - 1; ++gy)
        for (int gx = 0; gx < CW - 1; ++gx) {
            auto a = (uint32_t)(gy * CW + gx);
            out.mesh.indices.push_back(a);
            out.mesh.indices.push_back(a + 1);
            out.mesh.indices.push_back(a + (uint32_t)CW);
            out.mesh.indices.push_back(a + 1);
            out.mesh.indices.push_back(a + (uint32_t)CW + 1);
            out.mesh.indices.push_back(a + (uint32_t)CW);
        }
    return out;
}

struct IcoSphere {
    std::vector<V3> verts;
    std::vector<uint32_t> tris;
};

inline IcoSphere makeIcosphere(int subdiv) {
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
            auto idx = (uint32_t)verts.size();
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

inline void addBallEdges(SoftMesh& mesh, const std::vector<uint32_t>& tris) {
    const float* pos = mesh.mesh.pos.data();
    std::map<uint64_t, bool> seen;
    for (size_t f = 0; f < tris.size(); f += 3) {
        const uint32_t e[3][2] = {{tris[f], tris[f + 1]}, {tris[f + 1], tris[f + 2]}, {tris[f + 2], tris[f]}};
        for (const auto& p : e) {
            const int lo = p[0] < p[1] ? (int)p[0] : (int)p[1];
            const int hi = p[0] < p[1] ? (int)p[1] : (int)p[0];
            const uint64_t key = ((uint64_t)lo << 32) | (uint32_t)hi;
            if (seen.contains(key))
                continue;
            seen[key] = true;
            const size_t oa = (size_t)3 * lo, ob = (size_t)3 * hi;
            const float dx = pos[oa] - pos[ob], dy = pos[oa + 1] - pos[ob + 1], dz = pos[oa + 2] - pos[ob + 2];
            mesh.cons.push_back({lo, hi, std::sqrt(dx * dx + dy * dy + dz * dz), 0.5f});
        }
    }
}

inline void addAntipodalTies(SoftMesh& mesh) {
    const int n = mesh.mesh.vertexCount();
    const float* pos = mesh.mesh.pos.data();
    V3 c{0, 0, 0};
    for (int i = 0; i < n; ++i) {
        const size_t o = (size_t)3 * i;
        c = vAdd(c, V3{pos[o], pos[o + 1], pos[o + 2]});
    }
    c = vScale(c, 1.0f / (float)n);
    std::vector<V3> dir(n);
    for (int i = 0; i < n; ++i) {
        const size_t o = (size_t)3 * i;
        V3 d{pos[o] - c.x, pos[o + 1] - c.y, pos[o + 2] - c.z};
        dir[i] = vScale(d, 1.0f / vLen(d));
    }
    for (int i = 0; i < n; ++i) {
        int partner = -1;
        float bestDot = 1e30f;
        for (int j = 0; j < n; ++j) {
            if (j == i)
                continue;
            const float d = vDot(dir[j], dir[i]);
            if (d < bestDot) {
                bestDot = d;
                partner = j;
            }
        }
        if (partner <= i)
            continue;
        const size_t oa = (size_t)3 * i, ob = (size_t)3 * partner;
        const float dx = pos[oa] - pos[ob], dy = pos[oa + 1] - pos[ob + 1], dz = pos[oa + 2] - pos[ob + 2];
        mesh.cons.push_back({i, partner, std::sqrt(dx * dx + dy * dy + dz * dz), 0.5f});
    }
}

inline SoftMesh makeBall(float radius, float y0, int subdiv = 3) {
    IcoSphere ico = makeIcosphere(subdiv);
    const int n = (int)ico.verts.size();
    SoftMesh out;
    out.mesh.pos.assign((size_t)3 * n, 0.0f);
    out.mesh.nrm.assign((size_t)3 * n, 0.0f);
    out.mesh.col.assign((size_t)3 * n, 0.0f);
    out.mesh.indices = ico.tris;
    const V3 center{0, y0, 0};
    for (int i = 0; i < n; ++i) {
        const V3 dir = ico.verts[i];
        const V3 p = vAdd(center, vScale(dir, radius));
        const size_t o = (size_t)3 * i;
        out.mesh.pos[o] = p.x;
        out.mesh.pos[o + 1] = p.y;
        out.mesh.pos[o + 2] = p.z;
        out.mesh.nrm[o] = dir.x;
        out.mesh.nrm[o + 1] = dir.y;
        out.mesh.nrm[o + 2] = dir.z;
        out.mesh.col[o] = 0.85f;
        out.mesh.col[o + 1] = 0.35f;
        out.mesh.col[o + 2] = 0.25f;
    }
    addBallEdges(out, ico.tris);
    addAntipodalTies(out);
    return out;
}
