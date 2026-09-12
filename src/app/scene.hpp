// scene.hpp
// The simulation side of the app (no rendering): owns the rigid-body system (Jolt: ground
// + 50 clumping capsules) and supplies the two soft bodies (the cloth sheet + the ball) to
// the GPU. The soft bodies are stepped ON THE GPU (see Renderer + shaders/softbody.comp) —
// the scene only builds their initial state (vertices + joint constraints) and the pinned
// (held) timing; it does NOT integrate them.
//
// The cloth and the ball are the SAME kind of thing: identical sim::SoftBody objects (a
// vertex list + two-point distance-constraint joints), stepped by the same GPU solver with
// the same SimParams (mass/damping/stiffness/tension). No pressure — the ball is a plain
// soft body, just a different shape + joints than the cloth.
#pragma once
#include "app/rigid.hpp"
#include "math.hpp"
#include "sim/sim.hpp"
#include <vector>

class Scene {
public:
    // scene constants (meters, 60 Hz)
    static constexpr int kCW = 64, kCH = 64;           // cloth grid (CW*CH points)
    static constexpr int kHoldFrames = 150;            // frames the cloth stays pinned (held flat)
    static constexpr float kClothSpan = 8.0f;          // cloth width (m)
    static constexpr float kClothY0 = 10.0f;           // the cloth's held height (m)
    static constexpr int kBallLat = 32, kBallLon = 32; // sphere lat/lon divisions (fine enough
                                                       // to hug the 0.5 m capsules)
    static constexpr float kBallRadius = 1.5f;         // ball radius (m)
    static constexpr float kBallY0 = 12.0f;            // ball starting height (m) — above the capsule
                                                       // spawn cloud (max top ~9.4), so it falls onto
                                                       // the settled pile instead of through it
    static constexpr float kBallVol = 4.0f / 3.0f * kPi * kBallRadius * kBallRadius * kBallRadius;

    void init();

    // one frame of physics (the app calls these, in this order):
    void stepRigid(int n = 1); // step the capsules; unpin the cloth after kHoldFrames.
                               // (The soft bodies are stepped on the GPU — see Renderer.)

    void reset(); // re-pin the cloth + clear the frame count (the renderer re-uploads the
                  // soft bodies' initial state; the scene only tracks the pin/frame state)
    bool clothPinned() const { return m_pinned != 0; }

    // plain-data access for the renderer (no Jolt/Vulkan):
    const sim::SoftBody& cloth() const { return m_cloth; }
    const sim::SoftBody& ball() const { return m_ball; }
    const std::vector<sim::Triangle>& ballTris() const { return m_ballTris; }
    const RigidScene& rigid() const { return m_rigid; }

private:
    RigidScene m_rigid;
    // the two soft bodies: their initial state (vertices + joint constraints) is read by the
    // renderer to seed the GPU sim. They are NEVER stepped here (the GPU owns the sim state).
    sim::SoftBody m_cloth, m_ball;
    std::vector<sim::Triangle> m_ballTris; // the ball's triangles (render index buffer)
    int m_pinned = 1;
    int m_frames = 0;
};
