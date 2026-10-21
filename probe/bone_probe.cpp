// GPU-free probe: ECS + GLTF loader + collision shape solver. Queries rest vertices by bone name.
#include "ecs.hpp"
#include "gltf.hpp"

#include "../solvers/collision_shape_solver.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: bone_probe <file.glb> [bone name] [weight threshold=0.2]\n");
        std::printf("path is relative to assets/ (e.g. local/hashimoto.glb)\n");
        return 1;
    }
    std::string path = argv[1];
    if (path.rfind("assets/", 0) == 0)
        path = path.substr(7);
    const std::string bone = argc >= 3 ? argv[2] : "";
    const float threshold = argc >= 4 ? (float)std::atof(argv[3]) : 0.2f;

    entt::registry reg;
    const entt::entity root = reg.create();
    std::string err;
    if (loadGltf(reg, root, path, err) == entt::null) {
        std::printf("load: %s\n", err.c_str());
        return 1;
    }

    entt::entity skelEnt = entt::null;
    int nSkel = 0;
    reg.view<Skeleton>().each([&](entt::entity e, const Skeleton&) {
        skelEnt = e;
        ++nSkel;
    });
    if (skelEnt == entt::null) {
        std::printf("no skeleton found\n");
        return 1;
    }
    const Skeleton& s = reg.get<Skeleton>(skelEnt);
    std::printf("skeleton '%s': %zu joints, %zu bone names, %zu linked meshes\n", s.name.c_str(), s.joints.size(),
                s.boneNames.size(), s.meshes.size());

    int linkedMeshes = 0;
    int totalVerts = 0;
    for (entt::entity m : s.meshes) {
        const auto* gc = reg.try_get<Geometry>(m);
        if (!gc)
            continue;
        const Mesh& mesh = gc->mesh;
        if (mesh.joints.empty())
            continue;
        ++linkedMeshes;
        totalVerts += mesh.vertexCount();
    }
    std::printf("skinned: %d meshes, %d rest vertices\n", linkedMeshes, totalVerts);

    int jointIdx = -1;
    if (!bone.empty()) {
        for (size_t i = 0; i < s.boneNames.size(); ++i)
            if (s.boneNames[i] == bone)
                jointIdx = (int)i;
        if (jointIdx < 0) {
            std::printf("bone '%s' not found; available bones:\n", bone.c_str());
            for (size_t i = 0; i < s.boneNames.size(); ++i)
                if (!s.boneNames[i].empty())
                    std::printf("  %zu: %s\n", i, s.boneNames[i].c_str());
            return 1;
        }
    }

    // per-bone vertex counts (weight > threshold on any slot)
    std::vector<int> counts(s.boneNames.size(), 0);
    for (entt::entity m : s.meshes) {
        const auto* gc = reg.try_get<Geometry>(m);
        if (!gc)
            continue;
        const Mesh& mesh = gc->mesh;
        const int nV = mesh.vertexCount();
        if (mesh.joints.size() != (size_t)4 * nV || mesh.weights.size() != (size_t)4 * nV)
            continue;
        for (int v = 0; v < nV; ++v)
            for (int slot = 0; slot < 4; ++slot) {
                const uint32_t j = mesh.joints[(size_t)v * 4 + slot];
                if (j < s.boneNames.size() && mesh.weights[(size_t)v * 4 + slot] > threshold)
                    ++counts[j];
            }
    }
    if (jointIdx < 0) {
        for (size_t i = 0; i < counts.size(); ++i)
            if (counts[i] > 0)
                std::printf("  bone %zu '%s': %d vertices\n", i, s.boneNames[i].c_str(), counts[i]);
        return 0;
    }

    std::vector<float> cloud;
    for (entt::entity m : s.meshes) {
        const auto* gc = reg.try_get<Geometry>(m);
        if (!gc)
            continue;
        const Mesh& mesh = gc->mesh;
        const int nV = mesh.vertexCount();
        if (mesh.joints.size() != (size_t)4 * nV || mesh.weights.size() != (size_t)4 * nV)
            continue;
        for (int v = 0; v < nV; ++v) {
            bool hit = false;
            for (int slot = 0; slot < 4; ++slot)
                if (mesh.joints[(size_t)v * 4 + slot] == (uint32_t)jointIdx && mesh.weights[(size_t)v * 4 + slot] > threshold)
                    hit = true;
            if (!hit)
                continue;
            const float* p = &mesh.pos[(size_t)v * 3];
            cloud.insert(cloud.end(), p, p + 3);
        }
    }
    std::printf("bone '%s' (joint %d, weight > %.2f): %zu vertices\n", bone.c_str(), jointIdx, threshold, cloud.size() / 3);
    if (cloud.empty())
        return 1;

    float minx = 1e30f, miny = 1e30f, minz = 1e30f, maxx = -1e30f, maxy = -1e30f, maxz = -1e30f;
    for (size_t i = 0; i < cloud.size(); i += 3) {
        minx = std::min(minx, cloud[i]);
        maxx = std::max(maxx, cloud[i]);
        miny = std::min(miny, cloud[i + 1]);
        maxy = std::max(maxy, cloud[i + 1]);
        minz = std::min(minz, cloud[i + 2]);
        maxz = std::max(maxz, cloud[i + 2]);
    }
    std::printf("bbox: (%.4f, %.4f, %.4f) - (%.4f, %.4f, %.4f)\n", minx, miny, minz, maxx, maxy, maxz);

    const css::CapsuleFit fit = css::FitTaperedCapsule(cloud.data(), cloud.size() / 3);
    std::printf("%s\n", css::FormatFitReport(fit).c_str());
    return 0;
}
