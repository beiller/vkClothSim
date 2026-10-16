#pragma once
#include "math.hpp"
#include <array>

// the six view-proj matrices of a cube sitting at `pos` (Vulkan cubemap face order:
// +X -X +Y -Y +Z -Z), each face looking outward through a fixed 90 degree fov. Used by the
// point-light shadow cube and any future display cube (e.g. reflection probes).
inline std::array<Mat4, 6> cubeViewProj(V3 pos, float nearP, float farP) {
    struct Face {
        V3 right, up, back;
    };
    const Face faces[6] = {
        {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}},   {{0, 0, -1}, {0, 1, 0}, {-1, 0, 0}}, {{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}}, {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}},
    };
    const Mat4 proj = perspective(90.0f, 1.0f, nearP, farP);
    std::array<Mat4, 6> out{};
    for (int f = 0; f < 6; ++f) {
        const Face& fc = faces[f];
        Mat4 view{};
        view.m[0] = fc.right.x;
        view.m[1] = fc.up.x;
        view.m[2] = fc.back.x;
        view.m[4] = fc.right.y;
        view.m[5] = fc.up.y;
        view.m[6] = fc.back.y;
        view.m[8] = fc.right.z;
        view.m[9] = fc.up.z;
        view.m[10] = fc.back.z;
        view.m[12] = -vDot(fc.right, pos);
        view.m[13] = -vDot(fc.up, pos);
        view.m[14] = -vDot(fc.back, pos);
        view.m[15] = 1.0f;
        out[f] = mul4(proj, view);
    }
    return out;
}
