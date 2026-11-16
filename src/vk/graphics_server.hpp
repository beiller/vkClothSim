#pragma once
#include <entt/entt.hpp>
#include <vulkan/vulkan.h>

#include "gpu_sim/skin.hpp"
#include "gpu_sim/soft.hpp"
#include "vk/renderer.hpp"

class VkApp;

template <typename T>
static T readSetting(const entt::registry& reg, const T& def = {}) {
    for (auto [e, v] : reg.view<T>().each())
        return v;
    return def;
}

class GraphicsServer {
public:
    struct SimProfMs {
        float skel = 0.0f, soft = 0.0f;
        void reset() { skel = soft = 0.0f; }
    };

    void init(VkApp& app, bool withRenderer = true);
    void build(const entt::registry& reg);
    // Fixed-timestep split (see FRAME_PACING.md): the cloth sim runs per physics tick, skinning
    // per rendered frame (it reads whatever bone state the ECS holds — interpolated from phase 4 on).
    void stepSoftSim(entt::registry& reg, float dt);
    void stepSkinning(const entt::registry& reg);
    // Async pipeline frame boundary (FRAME_PACING.md phase 6): beginFrame picks this frame's ring slot and
    // waits on its fence (so overwriting the slot's buffers/sets is safe); endFrame flushes the batched GPU
    // stages with the slot's fence. Both are no-ops in --sync mode.
    void beginFrame();
    void endFrame();
    void draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, entt::registry& reg) {
        m_renderer.draw(cmd, app, fb, reg, readSetting<RenderSettings>(reg));
    }
    void drawVr(VkCommandBuffer cmd, VkApp& app, uint32_t fb, entt::registry& reg, entt::entity cam,
                const uint32_t* xrImg, int nXr) {
        m_renderer.drawVr(cmd, app, fb, reg, readSetting<RenderSettings>(reg), cam, xrImg, nXr);
    }
    void setViewProj(const CameraView& cv) { m_renderer.setViewProj(cv); }
    void setInterpAlpha(float a) { m_renderer.setInterpAlpha(a); }
    void initXrTarget(VkApp& app, VkFormat fmt, int nEyes, const std::vector<std::vector<VkImage>>& images,
                      const VkExtent2D* exts) {
        m_renderer.initXrTarget(app, fmt, nEyes, images, exts);
    }

    void uiEnsureTarget(const entt::registry& reg, entt::entity canvas) { m_renderer.uiEnsureTarget(reg, canvas); }
    VkRenderPass uiTargetPass(entt::entity canvas) const { return m_renderer.uiTargetPass(canvas); }
    VkFramebuffer uiTargetFb(entt::entity canvas) const { return m_renderer.uiTargetFb(canvas); }
    VkImage canvasImage(entt::entity e) const { return m_renderer.gpuTextureImage(e); }

    entt::entity uploadTexture(entt::registry& reg, entt::entity texEnt) { return m_renderer.uploadTexture(reg, texEnt); }
    void buildEnvironment(VkApp& app, entt::entity envEnt, entt::entity eqTex) {
        m_renderer.buildEnvironment(app, envEnt, eqTex);
    }
    void makeGpuTexture(entt::entity e, uint32_t w, uint32_t h) { m_renderer.makeGpuTexture(e, w, h); }
    void setFloor(float y) { m_soft.setFloor(y); }
    // per-vertex pinning (see Soft::setPinWeights for the return codes: 0 ok, 1 not a soft body, 2 bad length)
    int setPinWeights(entt::registry& reg, entt::entity e, const std::vector<float>& w) {
        return m_soft.setPinWeights(reg, e, w);
    }
    int setPinMap(entt::registry& reg, entt::entity e, const std::vector<uint32_t>& map) {
        return m_soft.setPinMap(reg, e, map);
    }
    void setPinSource(entt::registry& reg, entt::entity e, entt::entity src) { m_soft.setPinSource(reg, e, src); }
    void updatePins(entt::entity e) { m_soft.updatePins(e); }

    Renderer::ProfMs& prof() { return m_renderer.prof; }
    bool& profOn() { return m_renderer.profOn; }
    SimProfMs simProf;

    void flushFrees();
    void reset();
    void shutdown();
    void watch(entt::registry& reg);

private:
    struct FallbackGeom {
        VkBuffer pos = VK_NULL_HANDLE, nrm = VK_NULL_HANDLE;
        VkDeviceMemory posMem = VK_NULL_HANDLE, nrmMem = VK_NULL_HANDLE;
        uint32_t vtxCount = 0;
    };

    Renderer m_renderer;
    Soft m_soft;
    Skin m_skin;
    int m_slot = 0; // current async ring slot (see beginFrame)
    VkApp* m_app = nullptr;
    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    bool m_rendererOn = false;
    std::unordered_map<entt::entity, int> m_fbGeom;
    std::vector<FallbackGeom> m_fbGeoms;
};
