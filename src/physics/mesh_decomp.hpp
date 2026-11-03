#pragma once
#include "core/mesh.hpp"
#include "core/math.hpp"
#include <vector>

// Convex-decomposition budget. maxHulls = -1 lets CoACD choose; threshold is the concavity
// threshold in normalized units (smaller = more, tighter hulls).
struct DecompParams {
    int maxHulls = -1;
    int maxVertsPerHull = 256;
    float threshold = 0.05f;
};

// Decompose a triangle mesh into convex parts (CoACD, SIGGRAPH'22). Returns each part's
// vertices as a flat xyz-triple array in the mesh's local space (ready to feed a
// ConvexHullShape). On degenerate input (non-manifold / inconsistent orientation, which
// CoACD rejects) it falls back to a single hull of all vertices; returns empty when there
// is no usable geometry (< 4 verts).
std::vector<std::vector<float>> convexDecompose(const Mesh& mesh, const DecompParams& p);
