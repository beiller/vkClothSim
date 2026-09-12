// params.hpp
// The user-tunable soft-body simulation parameters. A leaf header (no deps): owned by the
// UI (UIState in ui.hpp), read by the scene (Scene::stepSoft) each step, and applied to
// EVERY soft body (the cloth + the ball) identically. Kept out of both so the UI never
// includes the scene and the scene never includes ImGui.
#pragma once

// The soft-body sim parameters (tuned in real time via the UI). Shared by all soft
// bodies: the same values drive the cloth and the ball identically.
struct SimParams {
    float mass = 1.0f;        // scales gravity (heavier = falls faster, drapes more)
    float damping = 0.96f;    // velocity damping per sub-step. The GPU Jacobi relax is less
                              // energy-stable than the CPU Gauss-Seidel, so it needs a lower
                              // damping to keep the pile impact from pumping energy (explode).
                              // 0.98 explodes ~300 frames; 0.96 is stable for 600+ frames.
    int stiffness = 8;        // Jacobi constraint relax passes per sub-step (higher = stiffer)
    float tension = 1.0f;     // rest-length scale (<1 loose/wrinkly, >1 taut)
};
