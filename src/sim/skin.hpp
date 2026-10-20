#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "gpuverts.hpp"
#include "math.hpp"
#include "sim/dualquat.hpp"
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>
#include <vector>

struct SkelGroupDesc {
    int nV = 0;
    std::vector<float> pos0;
    std::vector<float> nrm0;
    std::vector<uint32_t> joints;
    std::vector<float> weights;
    std::vector<float> deltas; // load-time staging for the GPU buffer, sourced from Mesh.shapes
};

class Skin {
public:
    static constexpr int kMaxActiveMorphs = 256; // a Daz character's full stack is 190-222 targets, all nonzero
    static constexpr int kPoolChunk = 16;

    void init(VkDevice dev, VkPhysicalDevice pdev);
    void addSkeleton(entt::entity e, int nJoints);
    int addGroup(entt::entity e, entt::entity firstMesh, const SkelGroupDesc& d);
    void linkMesh(entt::entity mesh, int groupId);
    MeshGpu groupOut(int groupId) const;
    void build();
    void step(const entt::registry& reg, VkCommandBuffer cmd);
    void destroySkeleton(entt::entity e);
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
    struct Group {
        int skelGpu = -1;
        int nV = 0;
        int activeCount = 0;
        bool dead = false;
        bool freed = false;
        std::vector<float> cpuPos;      // rest positions, 3 * nV
        std::vector<uint32_t> cpuJoints; // 4 * nV
        std::vector<float> cpuWeights;   // 4 * nV
        entt::entity firstMesh = entt::null; // shape source; shape weights are read from its Mesh
        Buf pos0, nrm0, joints, weights, deltas, activeMorphs, posOut, nrmOut;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    struct Skel {
        bool dead = false;
        std::vector<int> groups;
        Buf jointDQ;
        std::vector<DualQuat> dqStaging;
    };

    void makeGroupSet(Group& g);
    void ensurePoolSlot();
    void stepSkeleton(const entt::registry& reg, const Skeleton& s, Skel& c);
    void packMorphs(const entt::registry& reg, Group& g);
    int meshRefs(int groupId) const;
    void maybeFreeGroup(int groupId);
    void releaseGroupBufs(Group& g);
    std::unordered_map<entt::entity, int>::const_iterator skelFor(const entt::registry& reg, entt::entity skel) const;
    void onSkeletonDestroyed(entt::registry&, entt::entity e);
    void onMeshDestroyed(entt::registry&, entt::entity e);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    std::vector<Skel> m_skels;
    std::unordered_map<entt::entity, int> m_ent;
    std::unordered_map<entt::entity, int> m_meshGroup;
    std::vector<int> m_freeGroups;
    std::vector<Group> m_groups;
    VkPipeline m_pipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_dsl = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_pools;
    int m_poolCap = 0, m_poolUsed = 0;
    VkPipelineLayout m_pl = VK_NULL_HANDLE;
};
