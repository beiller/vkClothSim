// scene.cpp
#include "app/scene.hpp"

void Scene::init() {
    // the rigid bodies: the ground + the 50 dynamic capsules (Jolt)
    m_rigid.init();

    // the two soft bodies. They are the SAME kind of thing: identical sim::SoftBody
    // objects (initial vertices + joint constraints) stepped by the same GPU solver.
    // Only their shape/constraints differ — the cloth is a CW x CH grid sheet held flat
    // above the pile, the ball a UV sphere held in the air.
    std::vector<float> verts;
    std::vector<sim::Constraint> cons;
    sim::makeCloth(verts, cons, kCW, kCH, kClothSpan, kClothY0);
    m_cloth.init(verts.data(), kCW * kCH, std::move(cons));

    std::vector<float> bverts;
    std::vector<sim::Constraint> bcons;
    sim::makeBall(bverts, bcons, m_ballTris, kBallLat, kBallLon, kBallRadius, kBallY0);
    const int BN = 2 + (kBallLat - 1) * kBallLon;
    m_ball.init(bverts.data(), BN, std::move(bcons));
}

void Scene::stepRigid(int n) {
    m_rigid.step(n);
    m_frames += n;
    if (m_frames >= kHoldFrames)
        m_pinned = 0;
}

// Re-pin the cloth + clear the frame count. The soft bodies' GPU state is re-uploaded by the
// caller (Renderer::resetSoftBodies); the scene only tracks the pin/frame state.
void Scene::reset() {
    m_frames = 0;
    m_pinned = 1;
}
