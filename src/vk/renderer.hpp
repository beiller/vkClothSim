// renderer.hpp
// The renderer: the GPU GRAPHICS side of the app. Every mesh (the ground quad, the 50
// capsules, the cloth sheet, the ball) is drawn through ONE vertex+fragment shader pair
// (shaders/mesh_vert.vert + mesh_frag.frag): the vertices are indexed into a Vtx SSBO
// (pos + nrm + col) and there is no per-mesh vertex input. The sky is the render pass' clear
// color (no shader).
//
// It does NOT run the soft-body sim: that lives in sim/softsim.{hpp,cpp} (the "cloth sim"
// concern). The renderer reads the sim's OUTPUT to draw the soft bodies (SoftSim::posBuffer)
// and bakes the Jolt capsules (passed in each frame) into a Vtx buffer for the instanced
// capsule draw.
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

    // Record + submit the render (clear to the bg color, ground, capsules, cloth, ball, the
    // ImGui overlay) into framebuffer `fb` on the already-begun `cmd`. The soft-body sim must
    // already have been recorded into `cmd` (it runs before the render pass); the renderer
    // re-points the soft-body draws at the sim's current Vtx buffers. `caps` are the Jolt
    // capsule transforms (baked into the capsule Vtx buffer this frame).
    void draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui, const SoftSim& sim,
              const std::vector<CapsuleGPU>& caps);

private:
    // One indexed render mesh: the index buffer + its descriptor set (0: Vtx SSBO, 1: the
    // shared viewProj UBO) + the index count. The Vtx SSBO is owned by the sim (soft bodies)
    // or the renderer (the static ground + the per-frame capsule bake).
    struct Mesh {
        VkBuffer ibuf = VK_NULL_HANDLE;
        VkDeviceMemory ibmem = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        uint32_t idxCount = 0;
    };

    // Create the 4 meshes' index buffers + descriptor sets (all sharing the one UBO + DSL).
    void initGround();
    void initCapsules();
    void initSoftBodies(const Scene& scene, const SoftSim& sim);
    // Create one mesh's index buffer + descriptor set (Vtx buffer + range from the caller).
    void makeMesh(Mesh& m, const std::vector<uint32_t>& idx, VkBuffer vtxBuf, VkDeviceSize vtxRange);
    // Point a soft-body render descriptor at the sim's current Vtx buffer (re-done each
    // frame, since the sim ping-pongs posA/posB).
    void updateSoftRenderSet(Mesh& m, const SoftSim& sim, SoftSim::Body body);
    // Bake the capsules into the capsule Vtx buffer (world pos/nrm + the red albedo) for this
    // frame, from the capsule transforms + the cached local capsule geometry.
    void bakeCapsules(const std::vector<CapsuleGPU>& caps);
    // Draw one mesh (the pipeline is already bound): bind its descriptor set + index buffer.
    static void drawMesh(VkCommandBuffer cmd, VkPipelineLayout pl, const Mesh& m);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    VkRenderPass m_rp = VK_NULL_HANDLE;
    Mat4 m_vp{};
    int m_nCaps = 0;

    // the ONE shared viewProj UBO (constant; written once at init) + the ONE pipeline/DSL.
    VkBuffer m_uUbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_uUmem = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_dsl = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkPipelineLayout m_pl = VK_NULL_HANDLE;
    VkPipeline m_pipe = VK_NULL_HANDLE;

    // the renderer-owned Vtx buffers (the static ground + the per-frame capsule bake).
    VkBuffer m_groundVtx = VK_NULL_HANDLE;
    VkDeviceMemory m_groundVtxMem = VK_NULL_HANDLE;
    VkBuffer m_capsVtx = VK_NULL_HANDLE;
    VkDeviceMemory m_capsVtxMem = VK_NULL_HANDLE;
    std::vector<float> m_capsBase; // capsule local geometry (pos+nrm), 6 floats/vertex
    uint32_t m_vpc = 0;            // capsule verts per capsule

    // the four meshes (ground/caps/cloth/ball); their Vtx SSBOs are sim- or renderer-owned.
    Mesh m_ground;
    Mesh m_caps;
    Mesh m_cloth;
    Mesh m_ball;
};
