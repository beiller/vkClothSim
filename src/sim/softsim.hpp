#pragma once
#include <vulkan/vulkan.h>

#include "capsule.hpp"
#include "ecs.hpp"
#include "gpuverts.hpp"
#include "mesh.hpp"
#include "sim/params.hpp"
#include "sim/phys.hpp"
#include "sim/sim.hpp"
#include <vector>

class SoftSim {
public:
    void init(VkDevice dev, VkPhysicalDevice pdev);
    int addCapsule(const CapsuleParams& params);
    void setCapsulePose(int slot, const Transform& pose);
    int addSoftBody(const Mesh& mesh, const std::vector<sim::Constraint>& cons, const MeshGpu& rw);
    void build();
    void shutdown();
    void setPinned(int softId, int pinned);
    void setParams(int softId, const SimParams& params, int steps);
    void record(VkCommandBuffer cmd);
    void resetSoft(int softId);

private:
    struct GpuBody {
        int pinned = 0;
        SimParams params;
        int steps = kDefaultSteps;
        sim::SoftBody soft;
        VkBuffer pos = VK_NULL_HANDLE, nrm = VK_NULL_HANDLE, prev = VK_NULL_HANDLE, sub0 = VK_NULL_HANDLE;
        VkBuffer contactN = VK_NULL_HANDLE, contactL = VK_NULL_HANDLE;
        VkBuffer entries = VK_NULL_HANDLE, entryStart = VK_NULL_HANDLE, colorVerts = VK_NULL_HANDLE;
        VkBuffer tris = VK_NULL_HANDLE, triStart = VK_NULL_HANDLE, triList = VK_NULL_HANDLE;
        VkBuffer paramsBuf = VK_NULL_HANDLE;
        VkDeviceMemory posMem = VK_NULL_HANDLE, nrmMem = VK_NULL_HANDLE, prevMem = VK_NULL_HANDLE,
                       sub0Mem = VK_NULL_HANDLE;
        VkDeviceMemory paramsMem = VK_NULL_HANDLE;
        VkDeviceMemory contactNMem = VK_NULL_HANDLE, contactLMem = VK_NULL_HANDLE;
        VkDeviceMemory entriesMem = VK_NULL_HANDLE, entryStartMem = VK_NULL_HANDLE, colorVertsMem = VK_NULL_HANDLE;
        VkDeviceMemory trisMem = VK_NULL_HANDLE, triStartMem = VK_NULL_HANDLE, triListMem = VK_NULL_HANDLE;
        VkDescriptorSet simSet = VK_NULL_HANDLE;
    };

    void makeBodySet(GpuBody& b);
    static int modeBarriers(const GpuBody& b, int mode, VkBufferMemoryBarrier* out);
    void dispatch(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset);
    void recordMode(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset);
    void recordBody(VkCommandBuffer cmd, const GpuBody& b, int iters, int pinned);
    void resetBody(GpuBody& b);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    std::vector<CapsuleParams> m_colliders;
    std::vector<GpuBody> m_body;
    VkPipeline m_softPipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_softDsl = VK_NULL_HANDLE;
    VkDescriptorPool m_softPool = VK_NULL_HANDLE;
    VkPipelineLayout m_softPl = VK_NULL_HANDLE;
    VkBuffer m_capsInstances = VK_NULL_HANDLE;
    VkDeviceMemory m_capsInstancesMem = VK_NULL_HANDLE;
};
