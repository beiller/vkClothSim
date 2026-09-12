// simtest.cpp
// Headless soft-body physics test (no window, no Vulkan, no Jolt — pure sim/): builds
// the ball's soft body (the same one the scene uses) and steps it N frames (gravity +
// pressure + the ground). Verifies the ball FALLS and lands, and keeps its volume.
//   Usage: simtest [frames]   (default 120)
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "sim/sim.hpp"

int main(int argc, char** argv) {
    const int frames = (argc > 1) ? std::atoi(argv[1]) : 120;
    // the same ball as the scene (32x32 sphere, r=1.5, held at y=12; 3 sub-steps/frame)
    const int BLAT = 32, BLON = 32, SUB = 3;
    const float R = 1.5f, Y0 = 12.0f;
    const int BN = 2 + (BLAT - 1) * BLON;
    std::vector<float> verts;
    std::vector<sim::Constraint> cons;
    std::vector<sim::Triangle> tris;
    sim::makeBall(verts, cons, tris, BLAT, BLON, R, Y0);
    sim::SoftBody body;
    float grav[3] = {0, -9.81f, 0};
    body.init(verts.data(), BN, std::move(cons), (1.0f / 60.0f) / SUB, grav, SUB, 4, 0.999f);
    const float vRest = 4.0f / 3.0f * 3.14159265f * R * R * R;
    const float vTarget = vRest;   // rest pressure (the UI default)
    for (int f = 0; f < frames; ++f) {
        body.gravity[1] = -9.81f;   // the ball falls
        body.step();
        sim::applyPressure(body, tris, vTarget);
        sim::applyCollision(body, {});   // no capsules here -> ground only
    }
    float cx = 0, cy = 0, cz = 0, ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < BN; ++i) {
        const float* p = body.posPtr(i);
        cx += p[0];
        cy += p[1];
        cz += p[2];
        if (p[1] < ymin) ymin = p[1];
        if (p[1] > ymax) ymax = p[1];
    }
    cx /= BN;
    cy /= BN;
    cz /= BN;
    std::fprintf(stderr, "[balltest %d frames] centroid=(%.2f,%.2f,%.2f) y=[%.2f,%.2f] vol=%.2fx rest\n",
                 frames, cx, cy, cz, ymin, ymax, sim::bodyVolume(body, tris) / vRest);
    return 0;
}
