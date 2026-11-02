#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "gpu_sim/gpuverts.hpp"
#include "math.hpp"
#include "gpu_sim/dualquat.hpp"
#include <entt/entt.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Skin {
public:
    static constexpr int kMaxActiveMorphs = 256; // a Daz character's full stack is 190-222 targets, all nonzero
    static constexpr int kPoolChunk = 16;

    void init(VkDevice dev, VkPhysicalDevice pdev);
    void setRequestVerts(std::function<GpuMeshRef(const entt::registry&, const Mesh&, entt::entity)> request);
    void sync(const entt::registry& reg);
    void build(const entt::registry& reg);
    void step(const entt::registry& reg, VkCommandBuffer cmd);
    void reset();
    void watch(entt::registry& reg);
    void flushFrees();
    void shutdown();

private:
    struct Buf {
        VkBuffer b = VK_NULL_HANDLE;
        VkDeviceMemory m = VK_NULL_HANDLE;
        VkDeviceSize sz = 0;
    };
    // shared morph-delta buffer; character prims usually repeat the same vertex set + target stack,
    // so states with content-identical shapes share one allocation (refcounted)
    struct ShapeSet {
        uint64_t key = 0;
        int nV = 0, nT = 0;
        int refs = 0;
        Buf deltas;
    };
    // per linked-mesh GPU state, adopted lazily from the mesh's Geometry component
    struct MeshState {
        entt::entity mesh = entt::null;
        int skelGpu = -1;
        int nV = 0;
        int activeCount = 0;
        bool dead = false;
        bool freed = false;
        int shapeSet = -1;
        Buf pos0, nrm0, joints, weights, activeMorphs;
        MeshGpu out; // renderer-owned; the sim writes it, the renderer reads it
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    struct Skel {
        bool dead = false;
        std::vector<int> states;
        Buf jointDQ;
        std::vector<DualQuat> dqStaging;
        std::vector<float> prevQuat; // 4 per joint; sign reference (prev frame) for the joint DQ
    };

    void adoptState(const entt::registry& reg, const Skeleton& s, entt::entity mesh, int skelGpu);
    void makeStateSet(MeshState& st);
    void ensurePoolSlot();
    void stepSkeleton(const entt::registry& reg, const Skeleton& s, Skel& c);
    void packMorphs(const entt::registry& reg, MeshState& st);
    void maybeFreeState(int stateId);
    void releaseStateBufs(MeshState& st);
    void onSkeletonDestroyed(entt::registry&, entt::entity e);
    void onMeshDestroyed(entt::registry&, entt::entity e);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    std::vector<Skel> m_skels;
    std::unordered_map<entt::entity, int> m_ent;
    std::unordered_map<entt::entity, int> m_meshState;
    std::vector<int> m_freeStates;
    std::vector<MeshState> m_states;
    std::vector<ShapeSet> m_shapeSets;
    std::unordered_map<uint64_t, int> m_shapeKey;
    VkPipeline m_pipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_dsl = VK_NULL_HANDLE;
    std::function<GpuMeshRef(const entt::registry&, const Mesh&, entt::entity)> m_request;
    std::vector<VkDescriptorPool> m_pools;
    int m_poolCap = 0, m_poolUsed = 0;
    VkPipelineLayout m_pl = VK_NULL_HANDLE;
};
