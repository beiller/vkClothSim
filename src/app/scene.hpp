#pragma once
#include "app/geometry.hpp"
#include "app/rigid.hpp"
#include "math.hpp"

class Scene {
public:
    static constexpr int kCW = 64, kCH = 64;
    static constexpr int kHoldFrames = 150;
    static constexpr float kClothSpan = 8.0f;
    static constexpr float kClothY0 = 10.0f;
    static constexpr int kBallLat = 32, kBallLon = 32;
    static constexpr float kBallRadius = 1.5f;
    static constexpr float kBallY0 = 12.0f;
    static constexpr float kSingleSpan = 4.0f;
    static constexpr float kSingleY0 = 3.5f;

    void init(bool single = false);
    void stepRigid(int n = 1);
    void reset();
    bool clothPinned() const { return m_pinned != 0; }

    const SoftMesh& cloth() const { return m_cloth; }
    const SoftMesh& ball() const { return m_ball; }
    const RigidScene& rigid() const { return m_rigid; }

private:
    RigidScene m_rigid;
    SoftMesh m_cloth, m_ball;
    int m_pinned = 1;
    int m_frames = 0;
};
