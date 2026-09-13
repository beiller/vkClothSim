// params.hpp
// The user-tunable soft-body simulation parameters. A leaf header (no deps): owned by the
// UI (UIState in ui.hpp), read by the renderer each frame (the GPU sim), and applied to
// EVERY soft body (the cloth + the ball) identically. Kept out of both so the UI never
// includes the scene and the scene never includes ImGui.
#pragma once

// The soft-body sim parameters (tuned in real time via the UI). Shared by all soft
// bodies: the same values drive the cloth and the ball identically.
struct SimParams {
    float mass = 1.0f;      // scales gravity (heavier = falls faster, drapes more)
    float damping = 0.96f;  // velocity damping applied in the Verlet predict step
    int passes = 8;         // XPBD solver passes per sub-step (collide + relax iterations)
    float stiffness = 1.0f; // XPBD constraint stiffness scale (1 = stiff, <1 = compliant/soft)
    float tension = 1.0f;   // rest-length scale (<1 loose/wrinkly, >1 taut)
    float friction = 0.35f; // Coulomb friction coefficient applied in the velocity-update step
};
