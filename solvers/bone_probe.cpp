// GPU-free probe: ECS + GLTF loader + collision shape solver. Fits a compound of tapered
// capsules (k-means 3D clusters, one capsule chain each, normal-aware when normals are
// present) to the rest vertex cloud of each named bone (weight > threshold on any slot)
// and prints paste-ready kFits + kJoints lua tables (see assets/scenes/caps_fit.lua).
#include "ecs.hpp"
#include "gltf.hpp"

#include "collision_shape_solver.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: bone_probe <file.glb> [--threshold x] [bone name ...]\n");
        std::printf("path is relative to assets/ (e.g. local/hashimoto.glb)\n");
        return 1;
    }
    std::string path = argv[1];
    if (path.rfind("assets/", 0) == 0)
        path = path.substr(7);
    std::vector<std::string> bones;
    float threshold = 0.2f;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--threshold=", 0) == 0)
            threshold = (float)std::atof(a.c_str() + 12);
        else
            bones.push_back(a);
    }
    if (bones.empty()) {
        std::printf("no bone names given\n");
        return 1;
    }

    entt::registry reg;
    const entt::entity root = reg.create();
    std::string err;
    if (loadGltf(reg, root, path, err) == entt::null) {
        std::printf("load: %s\n", err.c_str());
        return 1;
    }

    entt::entity skelEnt = entt::null;
    reg.view<Skeleton>().each([&](entt::entity e, const Skeleton&) {
        skelEnt = e;
    });
    if (skelEnt == entt::null) {
        std::printf("no skeleton found\n");
        return 1;
    }
    const Skeleton& s = reg.get<Skeleton>(skelEnt);
    std::printf("-- skeleton '%s': %zu joints, %zu bone names, %zu linked meshes\n", s.name.c_str(), s.joints.size(),
                s.boneNames.size(), s.meshes.size());

    const auto findJoint = [&](const std::string& bone) {
        for (size_t i = 0; i < s.boneNames.size(); ++i)
            if (s.boneNames[i] == bone)
                return (int)i;
        return -1;
    };
    // rest positions + outward normals of every vertex weighted onto the joint above the
    // threshold; the normals are dropped (positions-only fit) if any mesh lacks them
    const auto boneCloud = [&](int jointIdx) {
        std::vector<float> pos, nrm;
        for (entt::entity m : s.meshes) {
            const auto* gc = reg.try_get<Geometry>(m);
            if (!gc)
                continue;
            const Mesh& mesh = gc->mesh;
            const int nV = mesh.vertexCount();
            if (mesh.joints.size() != (size_t)4 * nV || mesh.weights.size() != (size_t)4 * nV)
                continue;
            const bool hasNrm = mesh.nrm.size() == (size_t)3 * nV;
            for (int v = 0; v < nV; ++v) {
                bool hit = false;
                for (int slot = 0; slot < 4; ++slot)
                    if (mesh.joints[(size_t)v * 4 + slot] == (uint32_t)jointIdx && mesh.weights[(size_t)v * 4 + slot] > threshold)
                        hit = true;
                if (!hit)
                    continue;
                const float* p = &mesh.pos[(size_t)v * 3];
                pos.insert(pos.end(), p, p + 3);
                if (hasNrm) {
                    const float* q = &mesh.nrm[(size_t)v * 3];
                    nrm.insert(nrm.end(), q, q + 3);
                }
            }
        }
        if (nrm.size() != pos.size())
            nrm.clear();
        return std::pair<std::vector<float>, std::vector<float>>{std::move(pos), std::move(nrm)};
    };

    std::printf("local kFits = {\n");
    bool missing = false;
    for (const std::string& bone : bones) {
        const int jointIdx = findJoint(bone);
        if (jointIdx < 0) {
            std::printf("-- bone '%s' not found\n", bone.c_str());
            missing = true;
            continue;
        }
        const auto [cloud, normals] = boneCloud(jointIdx);
        if (cloud.empty())
            continue;

        float minx = 1e30f, miny = 1e30f, minz = 1e30f, maxx = -1e30f, maxy = -1e30f, maxz = -1e30f;
        for (size_t i = 0; i < cloud.size(); i += 3) {
            minx = std::min(minx, cloud[i]);
            maxx = std::max(maxx, cloud[i]);
            miny = std::min(miny, cloud[i + 1]);
            maxy = std::max(maxy, cloud[i + 1]);
            minz = std::min(minz, cloud[i + 2]);
            maxz = std::max(maxz, cloud[i + 2]);
        }
        std::printf("-- bbox: (%.4f, %.4f, %.4f) - (%.4f, %.4f, %.4f)\n", minx, miny, minz, maxx, maxy, maxz);

        css::FitOptions options;
        options.profileBins = 64;
        options.maxSegments = 8;
        options.profilePercentile = 1.0f; // per-bin max: the cloud is on-skin, the max is the body extent
        options.sphereAspect = 5.0f; // blob-like parts (malleolus, knee) become cap-less spheres, not chains whose caps poke past the blob
        if (const char* e = std::getenv("CSS_BINS"))
            options.profileBins = (int)std::atof(e);
        if (const char* e = std::getenv("CSS_SEGS"))
            options.maxSegments = (int)std::atof(e);
        if (const char* e = std::getenv("CSS_PCTL"))
            options.profilePercentile = (float)std::atof(e);
        if (const char* e = std::getenv("CSS_CLUSTERS"))
            options.maxClusters = (int)std::atof(e);
        const css::CompoundFit fit = css::FitTaperedCapsuleCompound(cloud.data(), normals.empty() ? nullptr : normals.data(), cloud.size() / 3, options);
        // post-process: shrink every radius to 0.8x so the shape hugs the skin from inside
        const float kRadiusScale = 0.8f;
        std::printf("-- bone '%s' (joint %d, weight > %.2f): %zu vertices%s, %zu capsules, underfit %.3f, overhang %.3f\n", bone.c_str(), jointIdx, threshold,
                    cloud.size() / 3, normals.empty() ? "" : " + normals", fit.parts.size(), fit.underfit, fit.overhang);
        if (!fit.ok) {
            std::printf("-- %s: fit failed\n", bone.c_str());
            continue;
        }
        std::printf("    { name = \"%s\", caps = {", bone.c_str());
        for (size_t c = 0; c < fit.parts.size(); ++c) {
            const css::CapsuleFit& part = fit.parts[c];
            char o[96], a[96];
            std::snprintf(o, sizeof o, "%.3f, %.3f, %.3f", part.origin.x, part.origin.y, part.origin.z);
            std::snprintf(a, sizeof a, "%.3f, %.3f, %.3f", part.axis.x, part.axis.y, part.axis.z);
            std::printf("%s\n      { origin = {%s}, axis = {%s}, seg = {", c ? "" : "", o, a);
            for (size_t i = 0; i < part.tapers.size(); ++i) {
                const css::Taper& t = part.tapers[i];
                std::printf("%s{%.3f, %.3f, %.3f, %.3f}", i ? ", " : "", t.s0, t.s1, t.r0 * kRadiusScale, t.r1 * kRadiusScale);
            }
            std::printf(" } }");
        }
        std::printf("\n    } },\n");
    }
    std::printf("}\n\n");

    // kJoints: every joint in the skeleton, with its parent bone + rest-pose world transform
    std::printf("local kJoints = {\n");
    const auto jointIndex = [&](entt::entity e) {
        for (size_t i = 0; i < s.joints.size(); ++i)
            if (s.joints[i] == e)
                return (int)i;
        return -1;
    };
    for (size_t i = 0; i < s.joints.size(); ++i) {
        const entt::entity j = s.joints[i];
        if (j == entt::null)
            continue;
        // nearest ancestor that is itself a joint = parent bone
        std::string parent;
        const auto* p = reg.try_get<Parent>(j);
        while (p) {
            const int pi = jointIndex(p->e);
            if (pi >= 0) {
                parent = s.boneNames[pi];
                break;
            }
            p = reg.try_get<Parent>(p->e);
        }
        const Transform wt = worldTransform(reg, j);
        std::printf("    { name = \"%s\", parent = \"%s\", pos = { %.4f, %.4f, %.4f }, quat = { %.4f, %.4f, %.4f, %.4f } },\n",
                    s.boneNames[i].c_str(), parent.c_str(),
                    wt.pos.x, wt.pos.y, wt.pos.z, wt.quat.x, wt.quat.y, wt.quat.z, wt.quat.w);
    }
    std::printf("}\n");
    return missing ? 1 : 0;
}
