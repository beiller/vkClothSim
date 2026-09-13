#pragma once
#include "mesh.hpp"
#include "sim/sim.hpp"

struct SoftMesh {
    Mesh mesh;
    std::vector<sim::Constraint> cons;
};

inline SoftMesh makeCloth(int CW, int CH, float span, float y0) {
    SoftMesh out;
    const int CN = CW * CH;
    out.mesh.vtx.assign((size_t)12 * CN, 0.0f);
    for (int gy = 0; gy < CH; ++gy)
        for (int gx = 0; gx < CW; ++gx) {
            int i = gy * CW + gx;
            size_t o = (size_t)12 * i;
            out.mesh.vtx[o] = -span / 2 + span * (float)gx / (float)(CW - 1);
            out.mesh.vtx[o + 1] = y0;
            out.mesh.vtx[o + 2] = -span / 2 + span * (float)gy / (float)(CH - 1);
            out.mesh.vtx[o + 4] = 1.0f;
            out.mesh.vtx[o + 8] = 0.25f;
            out.mesh.vtx[o + 9] = 0.45f;
            out.mesh.vtx[o + 10] = 0.78f;
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

inline SoftMesh makeBall(int lat, int lon, float radius, float y0) {
    SoftMesh out;
    auto vid = [lat, lon](int r, int c) {
        if (r == 0)
            return 0;
        if (r == lat)
            return 1 + (lat - 1) * lon;
        return 1 + (r - 1) * lon + (c % lon);
    };
    auto vpos = [lat, lon, radius](int r, int c, float& x, float& y, float& z) {
        if (r == 0) {
            x = 0;
            y = radius;
            z = 0;
            return;
        }
        if (r == lat) {
            x = 0;
            y = -radius;
            z = 0;
            return;
        }
        float phi = kPi * (float)r / (float)lat, theta = 2.0f * kPi * (float)c / (float)lon;
        x = radius * std::sin(phi) * std::cos(theta);
        y = radius * std::cos(phi);
        z = radius * std::sin(phi) * std::sin(theta);
    };
    const int BN = 2 + (lat - 1) * lon;
    out.mesh.vtx.assign((size_t)12 * BN, 0.0f);
    for (int r = 0; r <= lat; ++r)
        for (int c = 0; c < lon; ++c) {
            int i = vid(r, c);
            float x, y, z;
            vpos(r, c, x, y, z);
            size_t o = (size_t)12 * i;
            out.mesh.vtx[o] = x;
            out.mesh.vtx[o + 1] = y0 + y;
            out.mesh.vtx[o + 2] = z;
            out.mesh.vtx[o + 4] = x / radius;
            out.mesh.vtx[o + 5] = y / radius;
            out.mesh.vtx[o + 6] = z / radius;
            out.mesh.vtx[o + 8] = 0.85f;
            out.mesh.vtx[o + 9] = 0.35f;
            out.mesh.vtx[o + 10] = 0.25f;
        }
    auto bdist = [&](int r1, int c1, int r2, int c2) {
        float ax, ay, az, bx, by, bz;
        vpos(r1, c1, ax, ay, az);
        vpos(r2, c2, bx, by, bz);
        float dx = ax - bx, dy = ay - by, dz = az - bz;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    for (int r = 0; r < lat; ++r)
        for (int c = 0; c < lon; ++c) {
            int a = vid(r, c), b = vid(r, (c + 1) % lon), c2 = vid(r + 1, c), d = vid(r + 1, (c + 1) % lon);
            out.cons.push_back({a, b, bdist(r, c, r, (c + 1) % lon), 0.5f});
            out.cons.push_back({a, c2, bdist(r, c, r + 1, c), 0.5f});
            out.cons.push_back({a, d, bdist(r, c, r + 1, (c + 1) % lon), 0.5f});
            out.mesh.indices.push_back((uint32_t)a);
            out.mesh.indices.push_back((uint32_t)b);
            out.mesh.indices.push_back((uint32_t)d);
            out.mesh.indices.push_back((uint32_t)a);
            out.mesh.indices.push_back((uint32_t)d);
            out.mesh.indices.push_back((uint32_t)c2);
        }
    return out;
}
