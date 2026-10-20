#pragma once
#include <cstdint>
#include <vector>

struct Mesh {
    std::vector<float> pos;
    std::vector<float> nrm;
    std::vector<float> uv;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> joints; // 4 per vertex (skin), empty = not skinned
    std::vector<float> weights;   // 4 per vertex (skin), empty = not skinned
    int vertexCount() const { return (int)(pos.size() / 3); }
};
