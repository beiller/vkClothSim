#pragma once
#include <vulkan/vulkan.h>

#include "capsule.hpp"
#include "math.hpp"
#include "sim/softdraw.hpp"
#include <imgui.h>
#include <span>
#include <vector>

class VkApp;

class Renderer {
public:
    void init(VkApp& app, std::span<const SoftDraw> soft, int nCaps, const Mat4& viewProj);
    void shutdown();
    void draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui,
              std::span<const CapsuleGPU> caps);

private:
    struct Mesh {
        VkBuffer ibuf = VK_NULL_HANDLE;
        VkDeviceMemory ibmem = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        uint32_t idxCount = 0;
    };

    void makeMesh(Mesh& m, VkBuffer vtx, VkDeviceSize vtxSize, const uint32_t* idx, uint32_t idxCount);
    void bakeCapsules(std::span<const CapsuleGPU> caps);
    static void drawMesh(VkCommandBuffer cmd, VkPipelineLayout pl, const Mesh& m);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    VkRenderPass m_rp = VK_NULL_HANDLE;
    Mat4 m_vp{};
    int m_nCaps = 0;

    VkBuffer m_uUbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_uUmem = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_dsl = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkPipelineLayout m_pl = VK_NULL_HANDLE;
    VkPipeline m_pipe = VK_NULL_HANDLE;

    VkBuffer m_groundVtx = VK_NULL_HANDLE;
    VkDeviceMemory m_groundVtxMem = VK_NULL_HANDLE;
    VkBuffer m_capsVtx = VK_NULL_HANDLE;
    VkDeviceMemory m_capsVtxMem = VK_NULL_HANDLE;
    std::vector<float> m_capsBase;
    uint32_t m_vpc = 0;

    Mesh m_ground;
    Mesh m_caps;
    std::vector<Mesh> m_soft;
};
