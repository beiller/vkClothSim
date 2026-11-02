#pragma once
#include <entt/entt.hpp>
#include <vulkan/vulkan.h>

#include "sim/skin.hpp"
#include "sim/soft.hpp"
#include "vk/renderer.hpp"

class VkApp;

template <typename T>
static T readSetting(const entt::registry& reg, const T& def = {}) {
    for (auto [e, v] : reg.view<T>().each())
        return v;
    return def;
}

// GPU server: Vulkan renderer + GPU sim (soft PBD + DQ skinning) + canvas targets.
// updateSim owns the 60 Hz soft accumulator and the sim command buffers/submits.
class GraphicsServer {
public:
    struct SimProfMs {
        float skel = 0.0f, soft = 0.0f;
        void reset() { skel = soft = 0.0f; }
    };

    void init(VkApp& app, bool withRenderer = true);
    void build(const entt::registry& reg);
    void updateSim(entt::registry& reg, float dt);
    void draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, entt::registry& reg) {
        m_renderer.draw(cmd, app, fb, reg, readSetting<RenderSettings>(reg));
    }
    void drawVr(VkCommandBuffer cmd, VkApp& app, uint32_t fb, entt::registry& reg, entt::entity cam,
                const uint32_t* xrImg, int nXr) {
        m_renderer.drawVr(cmd, app, fb, reg, readSetting<RenderSettings>(reg), cam, xrImg, nXr);
    }
    void setViewProj(const CameraView& cv) { m_renderer.setViewProj(cv); }
    void initXrTarget(VkApp& app, VkFormat fmt, int nEyes, const std::vector<std::vector<VkImage>>& images,
                      const VkExtent2D* exts) {
        m_renderer.initXrTarget(app, fmt, nEyes, images, exts);
    }

    // canvas targets (ICanvasTarget for the UI layer)
    void uiEnsureTarget(const entt::registry& reg, entt::entity canvas) { m_renderer.uiEnsureTarget(reg, canvas); }
    VkRenderPass uiTargetPass(entt::entity canvas) const { return m_renderer.uiTargetPass(canvas); }
    VkFramebuffer uiTargetFb(entt::entity canvas) const { return m_renderer.uiTargetFb(canvas); }
    VkImage canvasImage(entt::entity e) const { return m_renderer.gpuTextureImage(e); }

    // scene loader / script
    entt::entity uploadTexture(entt::registry& reg, entt::entity texEnt) { return m_renderer.uploadTexture(reg, texEnt); }
    void buildEnvironment(VkApp& app, entt::entity envEnt, entt::entity eqTex) {
        m_renderer.buildEnvironment(app, envEnt, eqTex);
    }
    void makeGpuTexture(entt::entity e, uint32_t w, uint32_t h) { m_renderer.makeGpuTexture(e, w, h); }
    void setFloor(float y) { m_soft.setFloor(y); }

    Renderer::ProfMs& prof() { return m_renderer.prof; }
    bool& profOn() { return m_renderer.profOn; }
    SimProfMs simProf;

    void flushFrees();
    void reset();
    void shutdown();
    void watch(entt::registry& reg);

private:
    // headless (no renderer): this server owns the drawable vertex buffers itself
    struct FallbackGeom {
        VkBuffer pos = VK_NULL_HANDLE, nrm = VK_NULL_HANDLE;
        VkDeviceMemory posMem = VK_NULL_HANDLE, nrmMem = VK_NULL_HANDLE;
        uint32_t vtxCount = 0;
    };

    Renderer m_renderer;
    Soft m_soft;
    Skin m_skin;
    VkApp* m_app = nullptr;
    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    bool m_rendererOn = false;
    std::unordered_map<entt::entity, int> m_fbGeom;
    std::vector<FallbackGeom> m_fbGeoms;
    float m_softAcc = 0.0f;
};
