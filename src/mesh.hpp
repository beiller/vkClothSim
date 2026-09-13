#pragma once
#include <cstdint>
#include <vector>

struct Mesh {
    std::vector<float> vtx;
    std::vector<uint32_t> indices;
    int vertexCount() const { return (int)(vtx.size() / 12); }
};
