// penprobe.cpp
// Headless one-way-collision probe (no window, no Vulkan — Jolt + the sim only). The soft
// bodies (cloth + ball) run on the GPU in the app, but this tool steps them on the CPU with
// the sim.hpp Gauss-Seidel solver (the reference for the GPU sim) and reports how deep any
// soft-body vertex sits INSIDE a capsule (penetration = r - dist; >0 = inside the collider)
// + how many vertices are in contact (within 2 cm of a surface). If the one-way push-out
// works, pen stays at ~0 (the 1 cm skin) every frame; a growing pen means the collision is
// letting the soft body through. (The CPU relax is Gauss-Seidel; the GPU is Jacobi, but the
// collision logic is the same — so this is a clean reference for the push-out.)
//   Usage: penprobe [frames] [hold]   (default 400 frames, 150 hold)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "app/scene.hpp"

// Max capsule penetration of a soft body (m; >0 = inside) + the contact count.
static float maxPen(const sim::SoftBody& b, const std::vector<sim::Collider>& col, int& contact) {
    if (getenv("PENPROBE_NOPROBE")) { contact = 0; return 0.0f; }   // timing: skip the O(n*m) diagnostic
    float pen = 0.0f;
    contact = 0;
    for (int i = 0; i < b.numVertices(); ++i) {
        const float* p = b.posPtr(i);
        for (const auto& c : col) {
            float abx = c.b[0] - c.a[0], aby = c.b[1] - c.a[1], abz = c.b[2] - c.a[2];
            float t = ((p[0] - c.a[0]) * abx + (p[1] - c.a[1]) * aby + (p[2] - c.a[2]) * abz) /
                      std::fmax(abx * abx + aby * aby + abz * abz, 1e-6f);
            t = std::fmax(0.0f, std::fmin(1.0f, t));
            float dx = p[0] - (c.a[0] + abx * t), dy = p[1] - (c.a[1] + aby * t), dz = p[2] - (c.a[2] + abz * t);
            float d = c.r - std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > pen) pen = d;
            if (d > -0.02f) contact++;
        }
    }
    return pen;
}

// Step one soft body on the CPU (the sim.hpp Gauss-Seidel solver) — the reference for the
// GPU sim: push the shared params, then `kSubsteps` of { Verlet + relax + one-way collision }
// (per sub-step, so a fast body can't slip through between collision calls). A held (pinned)
// body is only kept clear of the colliders (no integration).
static void stepBodyCPU(sim::SoftBody& b, const SimParams& p, const std::vector<sim::Collider>& col, int pinned) {
    b.gravity[1] = -9.81f * p.mass;
    b.damping = p.damping;
    b.iterations = p.stiffness;
    b.tension = p.tension;
    if (pinned) {
        sim::applyCollision(b, col, 0.1f);
        return;
    }
    b.applyTension();
    for (int s = 0; s < Scene::kSubsteps; ++s) {
        b.substep();
        sim::applyCollision(b, col, 0.1f);
    }
}

int main(int argc, char** argv) {
    const int frames = (argc > 1) ? std::atoi(argv[1]) : 400;
    // Match the live app: the cloth stays pinned (held flat) for the first 150 frames while
    // the capsules fall + clump, then it falls onto the settled pile. (Use --hold 0 to drop
    // the cloth immediately instead.)
    const int hold = (argc > 2) ? std::atoi(argv[2]) : 150;
    const int stiff = (argc > 3) ? std::atoi(argv[3]) : -1;
    Scene scene;
    scene.init();
    SimParams p;          // the UI defaults
    if (stiff >= 0)
        p.stiffness = stiff;
    if (hold <= 0)
        scene.unpin();    // drop the cloth for the whole run (like --shot)
    // local CPU copies of the two soft bodies (the scene keeps the initial state; we step
    // these with the sim.hpp solver — the reference for the GPU sim).
    sim::SoftBody cloth = scene.cloth();
    sim::SoftBody ball = scene.ball();
    float worstC = 0.0f, worstB = 0.0f;
    int worstFC = -1, worstFB = -1;
    for (int f = 0; f < frames; ++f) {
        scene.stepRigid(1);
        const std::vector<sim::Collider> col = scene.rigid().colliders();
        const int pinned = scene.clothPinned() ? 1 : 0;
        stepBodyCPU(cloth, p, col, pinned);
        stepBodyCPU(ball, p, col, 0);
        int cc = 0, bc = 0;
        float cp = maxPen(cloth, col, cc);
        float bp = maxPen(ball, col, bc);
        if (cp > worstC) {
            worstC = cp; worstFC = f;
            // dump the details at the worst moment: the deepest cloth vertex + its velocity
            int vi = -1; float vd = -1e9f;
            for (int i = 0; i < cloth.numVertices(); ++i) {
                const float* pp = cloth.posPtr(i);
                for (const auto& c : col) {
                    float abx = c.b[0]-c.a[0], aby = c.b[1]-c.a[1], abz = c.b[2]-c.a[2];
                    float t = ((pp[0]-c.a[0])*abx+(pp[1]-c.a[1])*aby+(pp[2]-c.a[2])*abz)/
                              std::fmax(abx*abx+aby*aby+abz*abz, 1e-6f);
                    t = std::fmax(0.0f, std::fmin(1.0f, t));
                    float dx = pp[0]-(c.a[0]+abx*t), dy = pp[1]-(c.a[1]+aby*t), dz = pp[2]-(c.a[2]+abz*t);
                    float d = c.r - std::sqrt(dx*dx+dy*dy+dz*dz);
                    if (d > vd) { vd = d; vi = i; }
                }
            }
            const float* pp = cloth.posPtr(vi);
            const float* pq = cloth.prevPtr(vi);
            std::fprintf(stderr, "[worst@%d] cloth v%d p=(%.2f,%.2f,%.2f) vel=(%.2f,%.2f,%.2f) pen=%+.3f m\n",
                         f, vi, pp[0], pp[1], pp[2], (pp[0]-pq[0])*180, (pp[1]-pq[1])*180, (pp[2]-pq[2])*180, vd);
        }
        if (bp > worstB) { worstB = bp; worstFB = f; }
        // the cloth's y-range (to compare the drape height vs the CPU reference)
        float cylo = 1e9f, cyhi = -1e9f;
        for (int i = 0; i < cloth.numVertices(); ++i) {
            float yv = cloth.posPtr(i)[1];
            cylo = std::min(cylo, yv); cyhi = std::max(cyhi, yv);
        }
        if (f >= 40 && (f % 10 == 0 || f == frames - 1))
            std::fprintf(stderr, "[frame %3d] cloth y=[%.2f,%.2f] pen=%+.4f contact=%4d | ball pen=%+.4f contact=%3d\n",
                         f, cylo, cyhi, cp, cc, bp, bc);
    }
    std::fprintf(stderr, "[worst cloth] pen=%+.4f m at frame %d | [worst ball] pen=%+.4f m at frame %d\n",
                 worstC, worstFC, worstB, worstFB);
    const float worst = std::max(worstC, worstB);
    std::fprintf(stderr, "[worst over %d frames] pen=%+.4f m (exit 1 if > 2 cm)\n", frames, worst);
    return worst > 0.02f ? 1 : 0;
}
