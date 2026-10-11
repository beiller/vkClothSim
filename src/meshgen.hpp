#pragma once
#include "capsule.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include "sim/sim.hpp"
#include <algorithm>
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

// cloth grid: vertex grid + structural/shear constraints
inline Mesh makeClothMesh(int cw, int ch, float span, float y0) {
    Mesh out;
    const int CN = cw * ch;
    out.pos.assign((size_t)3 * CN, 0.0f);
    out.nrm.assign((size_t)3 * CN, 0.0f);
    out.uv.assign((size_t)2 * CN, 0.0f);
    for (int gy = 0; gy < ch; ++gy)
        for (int gx = 0; gx < cw; ++gx) {
            int i = gy * cw + gx;
            const float x = -span / 2 + span * (float)gx / (float)(cw - 1);
            const float z = -span / 2 + span * (float)gy / (float)(ch - 1);
            vStore(out.pos.data(), i, {x, y0, z});
            out.nrm[3 * i + 1] = 1.0f;
            out.uv[2 * i] = (float)gx / (float)(cw - 1);
            out.uv[2 * i + 1] = (float)gy / (float)(ch - 1);
        }
    for (int gy = 0; gy < ch - 1; ++gy)
        for (int gx = 0; gx < cw - 1; ++gx) {
            auto a = (uint32_t)(gy * cw + gx);
            out.indices.push_back(a);
            out.indices.push_back(a + (uint32_t)cw);
            out.indices.push_back(a + 1);
            out.indices.push_back(a + 1);
            out.indices.push_back(a + (uint32_t)cw);
            out.indices.push_back(a + (uint32_t)cw + 1);
        }
    return out;
}

inline std::vector<sim::Constraint> makeClothCons(int cw, int ch, float span) {
    std::vector<sim::Constraint> cons;
    const int CN = cw * ch;
    const float spacing = span / (float)(cw - 1);
    const float diag = spacing * kSqrt2;
    for (int i = 0; i < CN; ++i) {
        int gx = i % cw, gy = i / cw;
        if (gx < cw - 1)
            cons.push_back({i, i + 1, spacing, 1.0f});
        if (gy < ch - 1)
            cons.push_back({i, i + cw, spacing, 1.0f});
        if (gx < cw - 1 && gy < ch - 1)
            cons.push_back({i, i + cw + 1, diag, 1.0f});
        if (gx > 0 && gy < ch - 1)
            cons.push_back({i, i + cw - 1, diag, 1.0f});
    }
    return cons;
}

// soft-sphere constraints: edge springs + antipodal ties (holds shape against gravity)
inline std::vector<sim::Constraint> makeBallCons(const Mesh& mesh) {
    std::vector<sim::Constraint> cons;
    {
        const float* pos = mesh.pos.data();
        std::map<uint64_t, bool> seen;
        for (size_t f = 0; f < mesh.indices.size(); f += 3) {
            const uint32_t e[3][2] = {{mesh.indices[f], mesh.indices[f + 1]},
                                      {mesh.indices[f + 1], mesh.indices[f + 2]},
                                      {mesh.indices[f + 2], mesh.indices[f]}};
            for (const auto& p : e) {
                const int lo = p[0] < p[1] ? (int)p[0] : (int)p[1];
                const int hi = p[0] < p[1] ? (int)p[1] : (int)p[0];
                const uint64_t key = ((uint64_t)lo << 32) | (uint32_t)hi;
                if (seen.contains(key))
                    continue;
                seen[key] = true;
                cons.push_back({lo, hi, vLen(vSub(vAt(pos, lo), vAt(pos, hi))), 0.5f});
            }
        }
    }
    const int n = mesh.vertexCount();
    const float* pos = mesh.pos.data();
    V3 c{0, 0, 0};
    for (int i = 0; i < n; ++i)
        c = vAdd(c, vAt(pos, i));
    c = vScale(c, 1.0f / (float)n);
    std::vector<V3> dir(n);
    for (int i = 0; i < n; ++i)
        dir[i] = vNorm(vSub(vAt(pos, i), c));
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
        cons.push_back({i, partner, vLen(vSub(vAt(pos, i), vAt(pos, partner))), 0.5f});
    }
    return cons;
}

constexpr int kCapPhiSegs = 20;
constexpr int kCapYRows = 32;
constexpr uint32_t kCapVPC = (uint32_t)(kCapYRows + 1) * kCapPhiSegs;

inline Mesh makeCapsuleMesh(const CapsuleParams& shape, int nCaps) {
    Mesh m;
    const size_t vcount = (size_t)nCaps * kCapVPC;
    m.pos.assign(3 * vcount, 0.0f);
    m.nrm.assign(3 * vcount, 0.0f);
    m.uv.assign(2 * vcount, 0.0f);
    const int S = kCapPhiSegs, M = kCapYRows;
    const int VPC = (int)kCapVPC;
    const float R = shape.radius, H = shape.halfLen;
    const float top = H + R, bot = -H - R, twoPi = 2.0f * kPi;
    for (int ci = 0; ci < nCaps; ++ci) {
        const uint32_t capBase = (uint32_t)ci * VPC;
        for (int i = 0; i <= M; ++i) {
            const float y = top - (float)i / M * (top - bot);
            float dy = 0.0f;
            if (y >= H)
                dy = y - H;
            else if (y <= -H)
                dy = y + H;
            float r, dr;
            if (dy == 0.0f) {
                r = R;
                dr = 0.0f;
            } else {
                r = std::sqrt(std::max(0.0f, R * R - dy * dy));
                dr = (r > 1e-5f) ? -dy / r : 0.0f;
            }
            for (int j = 0; j < S; ++j) {
                const float phi = (float)j / S * twoPi;
                const float cp = std::cos(phi), sp = std::sin(phi);
                const int v = (int)(capBase + (size_t)i * S + (size_t)j);
                vStore(m.pos.data(), v, {r * cp, y, r * sp});
                vStore(m.nrm.data(), v, {cp, -dr, sp});
                m.uv[2 * v] = (float)j / S;
                m.uv[2 * v + 1] = (float)i / M;
            }
        }
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < S; ++j) {
                const auto jn = (uint32_t)((j + 1) % S);
                const uint32_t a = capBase + (uint32_t)i * S + (uint32_t)j;
                const uint32_t b = capBase + (uint32_t)i * S + jn;
                const uint32_t c = capBase + (uint32_t)(i + 1) * S + (uint32_t)j;
                const uint32_t d = capBase + (uint32_t)(i + 1) * S + jn;
                m.indices.push_back(a);
                m.indices.push_back(b);
                m.indices.push_back(d);
                m.indices.push_back(a);
                m.indices.push_back(d);
                m.indices.push_back(c);
            }
    }
    return m;
}
