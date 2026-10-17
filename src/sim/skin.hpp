#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "gpuverts.hpp"
#include "math.hpp"
#include "sim/dualquat.hpp"
#include <entt/entt.hpp>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// CPU data for a skinned group: the shared skin data of a set of primitives (one distinct
// POSITION accessor). Rest data, joints/weights and morph deltas are read once per group,
// never per primitive. Every primitive of a group shares the group's posOut/nrmOut.
struct SkelGroupDesc {
    int nV = 0;
    std::vector<float> pos0;        // nV*3
    std::vector<float> nrm0;        // nV*3
    std::vector<uint32_t> joints;   // nV*4
    std::vector<float> weights;     // nV*4 (renormalized to a convex blend)
    std::vector<float> deltas;      // nTargets*nV*3, target-major (a zero slot when a target has no POSITION)
    std::vector<std::string> morphNames;
    std::vector<float> initWeights; // nTargets: the file's static (frame-0) morph strengths; the startup + reset state
};

// GPU dual-quaternion skinning + shape keys. The ECS `Skeleton` component (on a character
// entity) is the source of truth for which skeletons exist and how they bind to the scene; this
// manager owns the Vulkan resources (one compute pipeline; per-character joint-DQ buffers;
// per-group vertex buffers + morph state) keyed by id, and step() discovers skeletons by viewing
// the `Skeleton` component, so any number of characters are supported. Groups and characters are
// registered at load time (before build); step() records one dispatch per group on the sim
// command buffer, before the soft sim, at the fixed sim rate.
class Skin {
public:
    static constexpr int kMaxActiveMorphs = 256; // a Daz character's full stack is 190-222 targets, all nonzero
    static constexpr int kPoolChunk = 16;        // descriptor pool growth step for runtime characters

    void init(VkDevice dev, VkPhysicalDevice pdev);
    // allocate a character's joint-DQ buffer (nJoints dual quaternions), keyed by the skeleton entity
    void addCharacter(entt::entity e, int nJoints);
    // allocate a group's vertex buffers, linked to the character's DQ buffer; returns the group id
    int addGroup(entt::entity e, const SkelGroupDesc& d);
    // the group's posOut/nrmOut as a MeshGpu; the renderer aliases these buffers (addMeshShared)
    MeshGpu groupOut(int groupId) const;
    void build();
    // per fixed step, BEFORE stepSoft: view the ECS `Skeleton` components, resolve each skeleton's
    // joint DQs from the ECS, pack active morphs, and dispatch one compute pass per group
    void step(const entt::registry& reg, VkCommandBuffer cmd);
    // morph weights live per group; a name applies to every group of the skeleton that has it
    void setMorph(const entt::registry& reg, entt::entity skel, const std::string& name, float weight);
    float getMorph(const entt::registry& reg, entt::entity skel, const std::string& name);
    void resetMorphs(const entt::registry& reg, entt::entity skel);
    void destroyCharacter(entt::entity e);
    // register the destroy observer: a Skeleton teardown retires the character. Its group
    // buffers are aliased by the renderer (addMeshShared), so they stay allocated and are
    // freed in bulk at shutdown, as before
    void watch(entt::registry& reg);
    void shutdown();

private:
    struct Buf {
        VkBuffer b = VK_NULL_HANDLE;
        VkDeviceMemory m = VK_NULL_HANDLE;
        VkDeviceSize sz = 0;
    };
    struct Group {
        int charGpu = -1;
        int nV = 0;
        int activeCount = 0;
        bool dead = false;
        std::vector<float> morphW;                    // current weight per target
        std::vector<float> morphW0;                   // the file's static weights (startup + reset state)
        std::map<std::string, int> nameToTarget;
        Buf pos0, nrm0, joints, weights, deltas, activeMorphs, posOut, nrmOut;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    struct Character {
        bool dead = false;
        std::vector<int> groups; // this character's group ids (the dispatch units)
        Buf jointDQ; // nJ x sizeof(DualQuat), host-visible (rewritten every step)
        std::vector<DualQuat> dqStaging;
    };

    void makeGroupSet(Group& g);
    void ensurePoolSlot(); // grow the descriptor pools in kPoolChunk steps when the pool is used up
    void stepCharacter(const entt::registry& reg, const Skeleton& s, Character& c);
    void packMorphs(Group& g);
    std::unordered_map<entt::entity, int>::const_iterator charFor(const entt::registry& reg, entt::entity skel) const;
    void onSkeletonDestroyed(entt::registry&, entt::entity e); // destroy-observer handler

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    std::vector<Character> m_chars; // keyed by charGpu
    std::unordered_map<entt::entity, int> m_ent; // skeleton entity -> charGpu
    std::vector<Group> m_groups;    // keyed by group id
    VkPipeline m_pipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_dsl = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_pools; // grown in kPoolChunk steps as runtime characters are added
    int m_poolCap = 0, m_poolUsed = 0;     // the pools.back() budget
    VkPipelineLayout m_pl = VK_NULL_HANDLE;
};
