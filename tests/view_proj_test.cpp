#include "core/math.hpp"
#include <cstdio>
#include <cmath>

// verifies the eyeVP pipeline (projFov * poseView) maps known world points to the
// expected NDC, using the lua_skin camera (pos 1.25,0.5,3; fov 50deg; identity quat)
int main() {
    const V3 camPos{1.25f, 0.5f, 3.0f};
    const Quat camQuat{1.0f, 0.0f, 0.0f, 0.0f};
    const float tanU = std::tan(50.0f * 0.5f * kPi / 180.0f);
    const float tanR = tanU; // square window, aspect 1
    const View v{camPos, camQuat, -tanR, tanR, tanU, -tanU};
    const Mat4 proj = projFov(v.tanL, v.tanR, v.tanU, v.tanD, 0.1f, 300.0f);
    const Mat4 view = poseView(v.pos, v.quat);
    const Mat4 M = proj * view;
    for (int r = 0; r < 4; ++r)
        std::printf("proj row%d: %.5f %.5f %.5f %.5f\n", r, proj[0][r], proj[1][r], proj[2][r], proj[3][r]);
    for (int r = 0; r < 4; ++r)
        std::printf("view row%d: %.5f %.5f %.5f %.5f\n", r, view[0][r], view[1][r], view[2][r], view[3][r]);

    const char* names[4] = {"center", "left-end", "right-end", "camera-pos"};
    const V3 pts[4] = {{1.25f, 0.5f, 0.0f}, {0.0f, 0.5f, 0.0f}, {2.5f, 0.5f, 0.0f}, {1.25f, 0.5f, 3.0f}};
    int fails = 0;
    for (int i = 0; i < 4; ++i) {
        const V4 clip = M * V4(pts[i], 1.0f);
        const float nx = clip.x / clip.w, ny = clip.y / clip.w;
        std::printf("%-10s -> ndc (%.3f, %.3f)\n", names[i], nx, ny);
    }
    // center must be at (0,0); camera-pos must be at z=0 (behind/at camera)
    const V4 c = M * V4(pts[0], 1.0f);
    if (std::fabs(c.x / c.w) > 1e-3f || std::fabs(c.y / c.w) > 1e-3f) {
        std::printf("FAIL: chain center not at ndc (0,0)\n");
        fails = 1;
    }
    const V4 cp = M * V4(pts[3], 1.0f);
    if (cp.z / cp.w > 1e-3f) {
        std::printf("FAIL: camera pos not at/behind camera (ndc z should be <=0, got %.3f)\n", cp.z / cp.w);
        fails = 1;
    }
    // rotated camera (90deg X): camera pos must map to origin in camera space, and the
    // forward point (pos + Q*(0,0,-1)*d) must map to (0,0,-d)
    {
        const Quat rq{0.70710678f, 0.70710678f, 0.0f, 0.0f}; // 90 deg about X (w, x)
        const V3 rp{1.0f, 2.0f, 3.0f};
        const Mat4 rv = poseView(rp, rq);
        const V3 fwd{0.0f, 1.0f, 0.0f}; // Q * (0,0,-1) for 90deg X
        const V4 camPos = rv * V4(rp, 1.0f);
        const V4 camFwd = rv * V4(rp + fwd * 5.0f, 1.0f);
        std::printf("rotated cam pos -> (%.4f, %.4f, %.4f)\n", camPos.x, camPos.y, camPos.z);
        std::printf("rotated fwd pt  -> (%.4f, %.4f, %.4f)\n", camFwd.x, camFwd.y, camFwd.z);
        if (glm::length(camPos - V4(0.0f, 0.0f, 0.0f, 1.0f)) > 1e-3f) {
            std::printf("FAIL: rotated cam pos not at camera-space origin\n");
            fails = 1;
        }
        if (glm::length(camFwd - V4(0.0f, 0.0f, -5.0f, 1.0f)) > 1e-3f) {
            std::printf("FAIL: rotated forward point not at (0,0,-5)\n");
            fails = 1;
        }
    }
    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails;
}
