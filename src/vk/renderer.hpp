#pragma once
#include <vulkan/vulkan.h>

#include "gpuverts.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include <imgui.h>
#include <vector>

class VkApp;

class Renderer {
public:
    struct GpuMeshRef {
        int geom;
        MeshGpu rw;
    };

    void init(VkApp& app, int nInstances, const Mat4& viewProj);
    GpuMeshRef addMesh(const Mesh& mesh);
    int addInstance(int geom);
    void setModel(int inst, const Mat4& model);
    void setMaterial(int inst, const V3& baseColor, float metallic, float roughness);
    void setViewProj(const Mat4& vp, const V3& camPos);
    void shutdown();
    void draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui, float exposure);

private:
    struct GpuMesh {
        VkBuffer pos = VK_NULL_HANDLE, nrm = VK_NULL_HANDLE, uv = VK_NULL_HANDLE;
        VkDeviceMemory posMem = VK_NULL_HANDLE, nrmMem = VK_NULL_HANDLE, uvMem = VK_NULL_HANDLE;
        VkBuffer ibuf = VK_NULL_HANDLE;
        VkDeviceMemory ibmem = VK_NULL_HANDLE;
        uint32_t idxCount = 0;
        uint32_t vtxCount = 0;
    };

    struct InstancedMesh {
        int geom = -1;
        VkBuffer modelUbuf = VK_NULL_HANDLE;
        VkDeviceMemory modelMem = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        Mat4 model{};
        V3 baseColor{1.0f, 1.0f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
    };

    static void drawInstance(VkCommandBuffer cmd, VkPipelineLayout pl, const GpuMesh& g, const InstancedMesh& inst);
    void rebuildTonemapSet(VkApp& app);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    VkRenderPass m_rp = VK_NULL_HANDLE;

    VkBuffer m_vpUbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_vpMem = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_dsl = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkPipelineLayout m_pl = VK_NULL_HANDLE;
    VkPipeline m_pipe = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_tmDsl = VK_NULL_HANDLE;
    VkDescriptorPool m_tmPool = VK_NULL_HANDLE;
    VkDescriptorSet m_tmSet = VK_NULL_HANDLE;
    VkPipelineLayout m_tmPl = VK_NULL_HANDLE;
    VkPipeline m_tmPipe = VK_NULL_HANDLE;
    VkBuffer m_tmUbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_tmMem = VK_NULL_HANDLE;
    VkImageView m_tmSrcView = VK_NULL_HANDLE;

    std::vector<GpuMesh> m_geoms;
    std::vector<InstancedMesh> m_insts;
};
