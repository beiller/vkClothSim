#pragma once
#include <vulkan/vulkan.h>

#include "capsule.hpp"
#include "components.hpp"
#include "gpu_sim/gpuverts.hpp"
#include "mesh.hpp"
#include "gpu_sim/phys.hpp"
#include "gpu_sim/sim.hpp"
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Soft {
public:
    void init(VkDevice dev, VkPhysicalDevice pdev);
    void setRequestVerts(std::function<GpuMeshRef(const entt::registry&, const Mesh&, entt::entity)> request);
    // lookup of existing GPU vertices (pin-target sources); slot < 0 when the entity has none
    void setVertsFor(std::function<GpuMeshRef(entt::entity)> lookup);
    void project(const entt::registry& reg);
    void setFloor(float y);
    void syncCapsules(const entt::registry& reg);
    static constexpr int kMaxCapsules = 16384;
    static constexpr int kPoolChunk = 16;
    CapsuleGPU* caps() { return m_caps.data(); }
    void uploadCapsules(int n);
    void addSoftBody(entt::entity e, const Mesh& mesh, const std::vector<sim::Constraint>& cons, const MeshGpu& rw,
                     const std::vector<float>& pinW, const std::vector<uint32_t>& pinMap,
                     const std::vector<float>& restLocal);
    void build();
    void reset();
    void shutdown();
    void record(VkCommandBuffer cmd, const entt::registry& reg, float dt);
    void watch(entt::registry& reg);
    void flushFrees();
    // per-vertex pinning. setPinWeights/setPinMap return 0 on success, 1 when e is not a soft body (or has no
    // geometry), 2 on a bad array length; they write the component and push to the GPU when the body exists.
    int setPinWeights(entt::registry& reg, entt::entity e, const std::vector<float>& w);
    int setPinMap(entt::registry& reg, entt::entity e, const std::vector<uint32_t>& map);
    void setPinSource(entt::registry& reg, entt::entity e, entt::entity src);
    // kept for API compatibility: pin targets now track their anchor's live WorldTransform every frame
    void updatePins(entt::entity e);

private:
    struct GpuBody {
        entt::entity ent{};
        bool dead = false;
        bool freed = false;
        sim::SoftBody soft;
        VkBuffer pos = VK_NULL_HANDLE, nrm = VK_NULL_HANDLE, prev = VK_NULL_HANDLE, sub0 = VK_NULL_HANDLE;
        VkBuffer contactN = VK_NULL_HANDLE, contactL = VK_NULL_HANDLE;
        VkBuffer entries = VK_NULL_HANDLE, entryStart = VK_NULL_HANDLE, colorVerts = VK_NULL_HANDLE;
        VkBuffer tris = VK_NULL_HANDLE, triStart = VK_NULL_HANDLE, triList = VK_NULL_HANDLE;
        VkBuffer paramsBuf = VK_NULL_HANDLE;
        VkBuffer pinW = VK_NULL_HANDLE, pinT = VK_NULL_HANDLE, pinMap = VK_NULL_HANDLE;
        VkBuffer restPos = VK_NULL_HANDLE; // local-space rest pose: the default pin anchor (restPose x WorldTransform)
        VkDeviceMemory posMem = VK_NULL_HANDLE, nrmMem = VK_NULL_HANDLE, prevMem = VK_NULL_HANDLE,
                        sub0Mem = VK_NULL_HANDLE;
        VkDeviceMemory paramsMem = VK_NULL_HANDLE;
        VkDeviceMemory contactNMem = VK_NULL_HANDLE, contactLMem = VK_NULL_HANDLE;
        VkDeviceMemory entriesMem = VK_NULL_HANDLE, entryStartMem = VK_NULL_HANDLE, colorVertsMem = VK_NULL_HANDLE;
        VkDeviceMemory trisMem = VK_NULL_HANDLE, triStartMem = VK_NULL_HANDLE, triListMem = VK_NULL_HANDLE;
        VkDeviceMemory pinWMem = VK_NULL_HANDLE, pinTMem = VK_NULL_HANDLE, pinMapMem = VK_NULL_HANDLE;
        VkDeviceMemory restPosMem = VK_NULL_HANDLE;
        bool hasPins = false; // any vertex weight > 0: pin targets re-derive from the anchor's live WorldTransform every frame
        VkDescriptorSet simSet = VK_NULL_HANDLE;
    };

    void makeBodySet(GpuBody& b);
    void freeBody(GpuBody& b);
    void onBodyDestroyed(entt::registry&, entt::entity e);
    std::function<GpuMeshRef(const entt::registry&, const Mesh&, entt::entity)> m_request;
    std::function<GpuMeshRef(entt::entity)> m_vertsFor;
    static int modeBarriers(const GpuBody& b, int mode, VkBufferMemoryBarrier* out);
    void dispatch(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset);
    void recordMode(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset);
    void recordBody(VkCommandBuffer cmd, const GpuBody& b, int iters, int pinned);
    // re-derive pin targets (src vertex -> pinT, anchor node's live world matrix) ahead of the sim dispatches;
    // runs every frame for bodies with pins
    void recordPinUpdates(VkCommandBuffer cmd, const entt::registry& reg);

    struct PinSet { VkDescriptorPool pool{}; VkDescriptorSet set{}; };

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
    // pin-target gather pipeline (one-shot descriptor sets: source pos buffer varies per update)
    VkPipeline m_pinPipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_pinDsl = VK_NULL_HANDLE;
    VkPipelineLayout m_pinPl = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_pinPools;
    int m_pinPoolCap = 0, m_pinPoolUsed = 0;
    std::vector<PinSet> m_pinSets; // sets allocated last frame; freed at the start of the next (queue is idle by then)
    std::unordered_set<entt::entity> m_pinWarned;
    VkBuffer m_capsInstances = VK_NULL_HANDLE;
    VkDeviceMemory m_capsInstancesMem = VK_NULL_HANDLE;
    float m_floorY = -std::numeric_limits<float>::infinity();
};
