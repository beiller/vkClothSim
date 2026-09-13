#pragma once
#include <vulkan/vulkan.h>

#include "capsule.hpp"
#include "mesh.hpp"
#include "sim/params.hpp"
#include "sim/phys.hpp"
#include "sim/sim.hpp"
#include "sim/softdraw.hpp"
#include <span>
#include <vector>

class SoftSim {
public:
    void init(VkDevice dev, VkPhysicalDevice pdev, int nCaps);
    void registerBody(const Mesh& mesh, const std::vector<sim::Constraint>& cons);
    void build();
    void shutdown();
    void uploadCapsules(std::span<const CapsuleGPU> caps);
    void record(VkCommandBuffer cmd, const SimParams& p, int pinnedMask);
    void reset(int mask);
    std::span<const SoftDraw> draws() const { return m_draw; }

private:
    struct GpuBody {
        sim::SoftBody soft;
        VkBuffer vtx = VK_NULL_HANDLE, prev = VK_NULL_HANDLE, sub0 = VK_NULL_HANDLE;
        VkBuffer contactN = VK_NULL_HANDLE, contactL = VK_NULL_HANDLE;
        VkBuffer entries = VK_NULL_HANDLE, entryStart = VK_NULL_HANDLE, colorVerts = VK_NULL_HANDLE;
        VkDeviceMemory vtxMem = VK_NULL_HANDLE, prevMem = VK_NULL_HANDLE, sub0Mem = VK_NULL_HANDLE;
        VkDeviceMemory contactNMem = VK_NULL_HANDLE, contactLMem = VK_NULL_HANDLE;
        VkDeviceMemory entriesMem = VK_NULL_HANDLE, entryStartMem = VK_NULL_HANDLE, colorVertsMem = VK_NULL_HANDLE;
        VkDescriptorSet simSet = VK_NULL_HANDLE;
    };

    void makeBodySet(GpuBody& b);
    void dispatch(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset);
    void recordMode(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset);
    void recordBody(VkCommandBuffer cmd, const GpuBody& b, int iters, int pinned);
    void resetBody(GpuBody& b);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    int m_nCaps = 0;
    std::vector<GpuBody> m_body;
    std::vector<SoftDraw> m_draw;
    VkPipeline m_softPipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_softDsl = VK_NULL_HANDLE;
    VkDescriptorPool m_softPool = VK_NULL_HANDLE;
    VkPipelineLayout m_softPl = VK_NULL_HANDLE;
    VkBuffer m_physParams = VK_NULL_HANDLE;
    VkDeviceMemory m_physParamsMem = VK_NULL_HANDLE;
    VkBuffer m_capsInstances = VK_NULL_HANDLE;
    VkDeviceMemory m_capsInstancesMem = VK_NULL_HANDLE;
};
