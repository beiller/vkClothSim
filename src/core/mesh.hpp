#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SubMesh {
    std::string name;
    float weight = 0.0f;
    std::vector<float> pos; // per-vertex offset, 3 * nV (empty = slot zeroed)
    std::vector<float> nrm; // per-vertex offset, optional
};

struct Mesh {
    std::vector<float> pos;
    std::vector<float> nrm;
    std::vector<float> uv;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> joints; // 4 per vertex (skin), empty = not skinned
    std::vector<float> weights;   // 4 per vertex (skin), empty = not skinned
    std::vector<SubMesh> shapes;  // vertex animation (morph targets)
    int vertexCount() const { return (int)(pos.size() / 3); }
};
