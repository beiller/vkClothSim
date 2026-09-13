#pragma once
#include <vulkan/vulkan.h>

#include "capsule.hpp"
#include "gpuverts.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include <imgui.h>
#include <span>
#include <vector>

class VkApp;

class Renderer {
public:
    void init(VkApp& app, int nCaps, int nSoft, const Mat4& viewProj);
    int addMesh(const Mesh& mesh);
    VertexStore positionBuffer(int handle) const;
    VertexStore normalBuffer(int handle) const;
    void shutdown();
    void draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui,
              std::span<const CapsuleGPU> caps);

private:
    struct GpuMesh {
        VkBuffer pos = VK_NULL_HANDLE, nrm = VK_NULL_HANDLE, col = VK_NULL_HANDLE;
        VkDeviceMemory posMem = VK_NULL_HANDLE, nrmMem = VK_NULL_HANDLE, colMem = VK_NULL_HANDLE;
        VkBuffer ibuf = VK_NULL_HANDLE;
        VkDeviceMemory ibmem = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        uint32_t idxCount = 0;
        uint32_t vtxCount = 0;
    };

    void makeMesh(GpuMesh& m, uint32_t vtxCount, const float* pos, const float* nrm, const float* col,
                  const uint32_t* idx, uint32_t idxCount);
    void bakeCapsules(std::span<const CapsuleGPU> caps);
    static void drawMesh(VkCommandBuffer cmd, VkPipelineLayout pl, const GpuMesh& m);

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

    std::vector<float> m_capsBase;
    uint32_t m_vpc = 0;

    GpuMesh m_ground;
    GpuMesh m_caps;
    std::vector<GpuMesh> m_soft;
};
