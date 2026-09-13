// softsim.hpp
// The GPU soft-body sim. Owns the compute shader, per-body sim state, Jolt-capsule colliders,
// and shared physics params. It records compute dispatches into a caller-provided command
// buffer and exposes position/capsule buffers for the renderer.
#pragma once
#include <vulkan/vulkan.h>

#include "app/params.hpp"
#include "app/rigid.hpp"
#include "sim/sim.hpp"
#include <cstdint>
#include <vector>

class SoftSim {
public:
    enum Body : std::uint8_t { kCloth = 0, kBall = 1, kCount = 2 };

    void init(VkDevice dev, VkPhysicalDevice pdev, const sim::SoftBody& cloth, const sim::SoftBody& ball, int nCaps);
    void shutdown();

    void uploadCapsules(const std::vector<CapsuleGPU>& instances);

    void record(VkCommandBuffer cmd, const SimParams& p, int clothPinned);

    void resetSoftBodies();
    void resetBall();

    VkBuffer posBuffer(Body body) const { return m_body[body].vtx; }
    int vertexCount(Body body) const { return m_body[body].n; }
    VkBuffer capsulesBuffer() const { return m_capsInstances; }
    int capsuleCount() const { return m_nCaps; }

private:
    struct GpuBody {
        VkBuffer vtx = VK_NULL_HANDLE, prev = VK_NULL_HANDLE, sub0 = VK_NULL_HANDLE;
        VkBuffer contactN = VK_NULL_HANDLE, contactL = VK_NULL_HANDLE;
        VkBuffer entries = VK_NULL_HANDLE, entryStart = VK_NULL_HANDLE, colorVerts = VK_NULL_HANDLE;
        VkDeviceMemory vtxMem = VK_NULL_HANDLE, prevMem = VK_NULL_HANDLE, sub0Mem = VK_NULL_HANDLE;
        VkDeviceMemory contactNMem = VK_NULL_HANDLE, contactLMem = VK_NULL_HANDLE;
        VkDeviceMemory entriesMem = VK_NULL_HANDLE, entryStartMem = VK_NULL_HANDLE, colorVertsMem = VK_NULL_HANDLE;
        VkDescriptorSet simSet = VK_NULL_HANDLE;
        int n = 0;
        int nEntries = 0;
        int colorCount = 0;
        std::vector<int> colorStart;
        std::vector<float> initPos;
        std::vector<float> initVtx;
    };

    void buildBodySimBuffers(GpuBody& b, const sim::SoftBody& body);
    void dispatch(VkCommandBuffer cmd, GpuBody& b, int mode, int n, int groupOffset);
    void recordMode(VkCommandBuffer cmd, GpuBody& b, int mode, int n, int groupOffset);
    void recordBody(VkCommandBuffer cmd, GpuBody& b, int iters, int pinned);
    void resetBody(GpuBody& b);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    int m_nCaps = 0;
    GpuBody m_body[kCount];
    VkPipeline m_softPipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_softDsl = VK_NULL_HANDLE;
    VkDescriptorPool m_softPool = VK_NULL_HANDLE;
    VkPipelineLayout m_softPl = VK_NULL_HANDLE;
    VkBuffer m_physParams = VK_NULL_HANDLE;
    VkDeviceMemory m_physParamsMem = VK_NULL_HANDLE;
    VkBuffer m_capsInstances = VK_NULL_HANDLE;
    VkDeviceMemory m_capsInstancesMem = VK_NULL_HANDLE;
};
