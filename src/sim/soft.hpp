#pragma once
#include <vulkan/vulkan.h>

#include "capsule.hpp"
#include "components.hpp"
#include "gpuverts.hpp"
#include "mesh.hpp"
#include "params.hpp"
#include "sim/phys.hpp"
#include "sim/sim.hpp"
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// a GPU mesh slot (owned by the renderer): slot index + read-write vertex stores
struct GpuMeshRef {
    int slot = -1;
    MeshGpu rw;
};

// narrow interface the sim uses to obtain GPU mesh slots (implemented by the renderer)
struct MeshProvider {
    std::function<GpuMeshRef(const Mesh&)> addMesh;
    std::function<void(entt::entity, int)> shareGeomSlot;
};

class Soft {
public:
    void init(VkDevice dev, VkPhysicalDevice pdev);
    void project(const entt::registry& reg, const MeshProvider& meshes);
    void setFloor(float y);
    void syncCapsules(const entt::registry& reg);
    static constexpr int kMaxCapsules = 16384;
    static constexpr int kPoolChunk = 16;
    CapsuleGPU* caps() { return m_caps.data(); }
    void uploadCapsules(int n);
    void addSoftBody(entt::entity e, const Mesh& mesh, const std::vector<sim::Constraint>& cons, const MeshGpu& rw);
    void build();
    void reset();
    void shutdown();
    void setPinned(entt::entity e, int pinned);
    void setParams(entt::entity e, const SimParams& params, int steps);
    void record(VkCommandBuffer cmd);
    void watch(entt::registry& reg);
    void flushFrees();

private:
    struct GpuBody {
        int pinned = 0;
        SimParams params;
        int steps = kDefaultSteps;
        bool dead = false;
        bool freed = false;
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
    void ensurePoolSlot();
    void freeBody(GpuBody& b);
    void onBodyDestroyed(entt::registry&, entt::entity e);
    static int modeBarriers(const GpuBody& b, int mode, VkBufferMemoryBarrier* out);
    void dispatch(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset);
    void recordMode(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset);
    void recordBody(VkCommandBuffer cmd, const GpuBody& b, int iters, int pinned);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    std::vector<CapsuleGPU> m_caps;
    int m_nCaps = 0;
    std::vector<GpuBody> m_body;
    std::unordered_map<entt::entity, int> m_ent;
    std::unordered_set<entt::entity> m_warned;
    std::vector<int> m_free;
    VkPipeline m_softPipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_softDsl = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_softPools;
    int m_softPoolCap = 0, m_softPoolUsed = 0;
    VkPipelineLayout m_softPl = VK_NULL_HANDLE;
    VkBuffer m_capsInstances = VK_NULL_HANDLE;
    VkDeviceMemory m_capsInstancesMem = VK_NULL_HANDLE;
    float m_floorY = -std::numeric_limits<float>::infinity();
};
