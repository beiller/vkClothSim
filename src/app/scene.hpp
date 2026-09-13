#pragma once
#include "app/geometry.hpp"
#include "app/rigid.hpp"
#include <vector>

class Scene {
public:
    static constexpr int kCW = 64, kCH = 64;
    static constexpr int kHoldFrames = 150;
    static constexpr float kClothSpan = 8.0f;
    static constexpr float kClothY0 = 10.0f;
    static constexpr int kBallSubdiv = 2;
    static constexpr float kBallRadius = 1.5f;
    static constexpr float kBallY0 = 12.0f;

    struct Soft {
        SoftMesh mesh;
        bool pinned = false;
    };

    void initRigid() { m_rigid.init(); }
    int add(SoftMesh mesh, bool pinned = false);
    int size() const { return (int)m_softs.size(); }
    const std::vector<Soft>& softs() const { return m_softs; }
    const RigidScene& rigid() const { return m_rigid; }
    void stepRigid(int n = 1);
    void reset() {
        m_frames = 0;
        m_pinned = true;
    }
    bool isPinned(int index) const { return m_pinned && m_softs[index].pinned; }
    int pinnedMask() const;

private:
    RigidScene m_rigid;
    std::vector<Soft> m_softs;
    int m_frames = 0;
    bool m_pinned = true;
};
