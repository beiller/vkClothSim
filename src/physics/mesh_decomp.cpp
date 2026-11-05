#include "physics/mesh_decomp.hpp"
#include "coacd.h"
#include <array>
#include <cstdio>
#include <iostream>
#include <stdexcept>

// CoACD (DISABLE_SPDLOG build) prints its config + progress to std::cout with no level
// filter; detach cout for the duration of a decomposition so it doesn't flood the app.
struct CoutGuard {
    std::streambuf* old;
    CoutGuard() : old(std::cout.rdbuf(nullptr)) {}
    ~CoutGuard() { std::cout.rdbuf(old); }
};

std::vector<std::vector<float>> convexDecompose(const Mesh& mesh, const DecompParams& p) {
    const int nV = (int)(mesh.pos.size() / 3);
    const int nT = (int)(mesh.indices.size() / 3);
    if (nV < 4 || nT == 0)
        return {}; // no usable geometry

    coacd::Mesh cm;
    cm.vertices.reserve(nV);
    for (int i = 0; i < nV; ++i)
        cm.vertices.push_back({mesh.pos[i * 3], mesh.pos[i * 3 + 1], mesh.pos[i * 3 + 2]});
    cm.indices.reserve(nT);
    for (int i = 0; i < nT; ++i)
        cm.indices.push_back({(int)mesh.indices[i * 3], (int)mesh.indices[i * 3 + 1], (int)mesh.indices[i * 3 + 2]});

    const auto toHulls = [](const std::vector<coacd::Mesh>& parts) {
        std::vector<std::vector<float>> out;
        out.reserve(parts.size());
        for (const auto& part : parts) {
            std::vector<float> pts;
            pts.reserve(part.vertices.size() * 3);
            for (const auto& v : part.vertices)
                pts.insert(pts.end(), {(float)v[0], (float)v[1], (float)v[2]});
            out.push_back(std::move(pts));
        }
        return out;
    };

    CoutGuard guard; // suppress CoACD's stdout during the call
    try {
        auto parts = coacd::CoACD(cm, p.threshold, p.maxHulls, "off",
                                 50, 2000, 20, 150, 3, false, true, false,
                                 p.maxVertsPerHull, false, 0.01, "ch", 0, false);
        if (!parts.empty())
            return toHulls(parts);
        std::fprintf(stderr, "3dsim: convex_decompose: CoACD produced no parts for %d-vert mesh\n", nV);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "3dsim: convex_decompose: CoACD failed on %d-vert mesh: %s\n", nV, e.what());
    }

    // fallback: one hull of every vertex; only sane for small meshes (a full-mesh hull makes Jolt's narrowphase unusably slow)
    if (nV > p.maxVertsPerHull)
        return {};
    return { mesh.pos };
}
