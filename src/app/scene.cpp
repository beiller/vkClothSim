#include "app/scene.hpp"

void Scene::init(bool single) {
    m_rigid.init(single);
    const float span = single ? kSingleSpan : kClothSpan;
    const float y0 = single ? kSingleY0 : kClothY0;
    m_cloth = makeCloth(kCW, kCH, span, y0);
    m_ball = makeBall(kBallLat, kBallLon, kBallRadius, kBallY0);
}

void Scene::stepRigid(int n) {
    m_rigid.step(n);
    m_frames += n;
    if (m_frames >= kHoldFrames)
        m_pinned = 0;
}

void Scene::reset() {
    m_frames = 0;
    m_pinned = 1;
}
