// scene.cpp
#include "app/scene.hpp"

void Scene::init() {
    // the rigid bodies: the ground + the 50 dynamic capsules (Jolt)
    m_rigid.init();

    // the two soft bodies. They are the SAME kind of thing: identical sim::SoftBody
    // objects with the SAME universal solver + the SAME default parameters (= the UI
    // defaults). Only their shape/constraints differ — the cloth is a CW x CH grid sheet
    // held flat above the pile, the ball a UV sphere held in the air. (The solver keeps
    // each body's base rest lengths and scales them by its tension each step.)
    SimParams def;
    float grav[3] = {0, -9.81f, 0};
    const float dt = (1.0f / 60.0f) / kSubsteps;

    std::vector<float> verts;
    std::vector<sim::Constraint> cons;
    sim::makeCloth(verts, cons, kCW, kCH, kClothSpan, kClothY0);
    m_cloth.init(verts.data(), kCW * kCH, std::move(cons), dt, grav, kSubsteps,
                 def.stiffness, def.damping);

    std::vector<float> bverts;
    std::vector<sim::Constraint> bcons;
    sim::makeBall(bverts, bcons, m_ballTris, kBallLat, kBallLon, kBallRadius, kBallY0);
    const int BN = 2 + (kBallLat - 1) * kBallLon;
    m_ball.init(bverts.data(), BN, std::move(bcons), dt, grav, kSubsteps,
                def.stiffness, def.damping);
}

void Scene::stepRigid(int n) {
    m_rigid.step(n);
    m_frames += n;
    if (m_frames >= kHoldFrames)
        m_pinned = 0;
}

void Scene::unpin() {
    m_pinned = 0;
}

// Re-pin the cloth + clear the frame count. The soft bodies' GPU state is re-uploaded by the
// caller (Renderer::resetSoftBodies); the scene only tracks the pin/frame state.
void Scene::reset() {
    m_frames = 0;
    m_pinned = 1;
}
