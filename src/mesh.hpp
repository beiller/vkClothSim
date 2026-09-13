#pragma once
#include <cstdint>
#include <vector>

struct Mesh {
    std::vector<float> pos;
    std::vector<float> nrm;
    std::vector<float> col;
    std::vector<uint32_t> indices;
    int vertexCount() const { return (int)(pos.size() / 3); }
};
