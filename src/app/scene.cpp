#include "app/scene.hpp"

int Scene::add(SoftMesh mesh, bool pinned) {
    m_softs.push_back({std::move(mesh), pinned});
    return (int)m_softs.size() - 1;
}

void Scene::stepRigid(int n) {
    m_rigid.step(n);
    m_frames += n;
    if (m_frames >= kHoldFrames)
        m_pinned = false;
}

int Scene::pinnedMask() const {
    if (!m_pinned)
        return 0;
    int mask = 0;
    const int n = (int)m_softs.size();
    for (int i = 0; i < n; ++i)
        if (m_softs[i].pinned)
            mask |= 1 << i;
    return mask;
}
