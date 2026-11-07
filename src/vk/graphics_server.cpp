#include "vk/graphics_server.hpp"

#include <algorithm>
#include <chrono>

#include "vk/vkapp.hpp"
#include "vk/vkutil.hpp"
#include "util.hpp"

void GraphicsServer::init(VkApp& app, bool withRenderer) {
    m_app = &app;
    m_dev = app.device();
    m_pdev = app.pdev();
    m_soft.init(app.device(), app.pdev());
    m_skin.init(app.device(), app.pdev());
    const auto requestVerts = [this](const entt::registry& reg, const Mesh& m, entt::entity key) {
        if (m_rendererOn)
            return m_renderer.requestVerts(reg, m, key);
        auto it = m_fbGeom.find(key);
        if (it == m_fbGeom.end()) {
            FallbackGeom g;
            const uint32_t n = (uint32_t)m.vertexCount();
            g.vtxCount = n;
            vkMakeBufferDev(m_dev, m_pdev, g.pos, g.posMem, 12 * (VkDeviceSize)n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           m.pos.data());
            vkMakeBufferDev(m_dev, m_pdev, g.nrm, g.nrmMem, 12 * (VkDeviceSize)n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           m.nrm.data());
            it = m_fbGeom.emplace(key, (int)m_fbGeoms.size()).first;
            m_fbGeoms.push_back(g);
        }
        const FallbackGeom& g = m_fbGeoms[(size_t)it->second];
        GpuMeshRef ref{-1, MeshGpu{VertexStore{g.pos, g.posMem, g.vtxCount}, VertexStore{g.nrm, g.nrmMem, g.vtxCount}}};
        return ref;
    };
    m_skin.setRequestVerts(requestVerts);
    m_soft.setRequestVerts(requestVerts);
    if (withRenderer) {
        m_renderer.init(app, 256, Mat4(1.0f));
        m_rendererOn = true;
    }
}

void GraphicsServer::build(const entt::registry& reg) {
    m_soft.build();
    m_skin.build(reg);
}

void GraphicsServer::updateSim(entt::registry& reg, float dt) {
    const auto tS = std::chrono::steady_clock::now();
    VkCommandBuffer skelCmd = m_app->beginCommands();
    m_skin.step(reg, skelCmd);
    m_app->submit(skelCmd);
    simProf.skel += msBetween(tS, std::chrono::steady_clock::now());

    // one soft step per frame at render rate; dt clamped so a stall can't produce a huge unstable cloth step
    const auto tC = std::chrono::steady_clock::now();
    VkCommandBuffer softCmd = m_app->beginCommands();
    m_soft.syncCapsules(reg);
    m_soft.project(reg);
    m_soft.record(softCmd, reg, std::min(dt, kMaxPhysDt));
    m_app->submit(softCmd);
    simProf.soft += msBetween(tC, std::chrono::steady_clock::now());
}

void GraphicsServer::flushFrees() {
    m_renderer.flushFrees();
    m_soft.flushFrees();
    m_skin.flushFrees();
}

void GraphicsServer::reset() {
    m_soft.reset();
    m_skin.reset();
    m_renderer.reset();
    for (auto& g : m_fbGeoms) {
        vkFreeBuffer(m_dev, g.pos, g.posMem);
        vkFreeBuffer(m_dev, g.nrm, g.nrmMem);
    }
    m_fbGeoms.clear();
    m_fbGeom.clear();
}

void GraphicsServer::shutdown() {
    m_soft.shutdown();
    m_skin.shutdown();
    for (auto& g : m_fbGeoms) {
        vkFreeBuffer(m_dev, g.pos, g.posMem);
        vkFreeBuffer(m_dev, g.nrm, g.nrmMem);
    }
    m_fbGeoms.clear();
    m_fbGeom.clear();
    m_renderer.shutdown();
}

void GraphicsServer::watch(entt::registry& reg) {
    m_renderer.watch(reg);
    m_soft.watch(reg);
    m_skin.watch(reg);
}
