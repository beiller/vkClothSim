// renderer.hpp
// The renderer: the GPU GRAPHICS side of the app. Owns the per-mesh graphics pipelines +
// buffers (the full-screen background, the instanced capsules + ground, and the two soft
// bodies — the cloth sheet + the ball) and the per-frame draw.
//
// It does NOT run the soft-body sim: that lives in sim/softsim.{hpp,cpp} (the "cloth sim"
// concern). The renderer reads the sim's OUTPUT to draw it — the current position buffers
// (SoftSim::posBuffer) for the soft bodies and the capsule collider buffer
// (SoftSim::capsulesBuffer) for the instanced capsule draw.
#pragma once
#include <vulkan/vulkan.h>

#include "math.hpp"
#include "sim/softsim.hpp"
#include <imgui.h> // ImDrawData (the optional overlay, drawn last)
#include <vector>

class VkApp;
class Scene;

class Renderer {
public:
    void init(VkApp& app, const Scene& scene, const SoftSim& sim, const Mat4& viewProj);
    void shutdown();

    void setViewProj(const Mat4& vp) { m_vp = vp; }
    // Record + submit the render (background, capsules, cloth, ball, the ImGui overlay) into
    // framebuffer `fb` on the already-begun `cmd`. The soft-body sim must already have been
    // recorded into `cmd` (it runs before the render pass); the renderer re-points the
    // soft-body draws at the sim's current position buffers.
    void draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui, const SoftSim& sim);

private:
    // One indexed render mesh: the pos SSBO (soft bodies) or vertex buffer (capsules) + the
    // index buffer + the 80-byte UBO + the pipeline + the render descriptor set.
    struct Mesh {
        VkBuffer vbuf = VK_NULL_HANDLE;
        VkDeviceMemory vbmem = VK_NULL_HANDLE;
        VkBuffer sbuf = VK_NULL_HANDLE;
        VkDeviceMemory sbmem = VK_NULL_HANDLE;
        VkBuffer ibuf = VK_NULL_HANDLE;
        VkDeviceMemory ibmem = VK_NULL_HANDLE;
        VkBuffer ubuf = VK_NULL_HANDLE;
        VkDeviceMemory ubmem = VK_NULL_HANDLE;
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkPipelineLayout pl = VK_NULL_HANDLE;
        VkPipeline pipe = VK_NULL_HANDLE;
        uint32_t idxCount = 0;
    };

    void initBackground();
    void initCapsules(const SoftSim& sim);
    void initSoftBodies(const Scene& scene, const SoftSim& sim); // build the cloth + ball render meshes
    // The cloth + ball share the render descriptor layout (0: pos SSBO, 1: UBO): create the
    // descriptor set layout + pool + set + pipeline layout for one of them. `posBuf` (owned
    // by the sim) is binding 0; it is re-pointed each frame by updateSoftRenderSet.
    static void makePosUboDescriptorSet(VkDevice dev, Mesh& m, VkBuffer posBuf, VkDeviceSize posRange);
    // The cloth + ball share the render mesh path too: the index buffer + the 80-byte UBO
    // (viewProj + `extraUbo` at offset 64) + the descriptor set + the pipeline (no vertex
    // input — the positions come from the sim's pos SSBO, which it owns).
    void makeBodyRenderMesh(Mesh& m, const SoftSim& sim, SoftSim::Body body, const std::vector<uint32_t>& idx,
                            const void* vertSpv, uint32_t vertLen, const void* fragSpv, uint32_t fragLen,
                            const float* extraUbo, size_t extraUboBytes);
    // Point a soft-body render descriptor at the sim's current position buffer (re-done each
    // frame, since the sim ping-pongs posA/posB).
    void updateSoftRenderSet(Mesh& m, const SoftSim& sim, SoftSim::Body body);
    static void drawMesh(VkCommandBuffer cmd, const Mesh& m, VkBuffer* vbuf);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    VkRenderPass m_rp = VK_NULL_HANDLE;
    Mat4 m_vp{};

    // background (full-screen triangle; its own VBO + color UBO)
    VkBuffer m_bgUbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_bgUmem = VK_NULL_HANDLE;
    VkBuffer m_bgVbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_bgVmem = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_bgDsl = VK_NULL_HANDLE;
    VkDescriptorPool m_bgPool = VK_NULL_HANDLE;
    VkDescriptorSet m_bgSet = VK_NULL_HANDLE;
    VkPipelineLayout m_bgPl = VK_NULL_HANDLE;
    VkPipeline m_bgPipe = VK_NULL_HANDLE;

    // the instanced capsule + ground mesh (its capsule SSBO is the sim's collider buffer)
    Mesh m_caps;
    int m_nCaps = 0;

    // the two soft bodies' render meshes (their position buffers live in the sim)
    Mesh m_clothMesh;
    Mesh m_ballMesh;
};
