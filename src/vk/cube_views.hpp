#pragma once
#include "math.hpp"
#include <array>

inline std::array<Mat4, 6> cubeViewProj(V3 pos, float nearP, float farP) {
    struct Face {
        V3 right, up, back;
    };
    const Face faces[6] = {
        {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}},   {{0, 0, -1}, {0, 1, 0}, {-1, 0, 0}}, {{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}}, {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}},
    };
    const Mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, nearP, farP);
    std::array<Mat4, 6> out{};
    for (int f = 0; f < 6; ++f) {
        const Face& fc = faces[f];
        const V3 tr{-glm::dot(fc.right, pos), -glm::dot(fc.up, pos), -glm::dot(fc.back, pos)};
        const Mat4 view(fc.right.x, fc.up.x, fc.back.x, 0.0f,
                       fc.right.y, fc.up.y, fc.back.y, 0.0f,
                       fc.right.z, fc.up.z, fc.back.z, 0.0f,
                       tr.x, tr.y, tr.z, 1.0f);
        out[f] = proj * view;
    }
    return out;
}
