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

// GPU dual-quaternion skinning + shape keys. The ECS `Skeleton` component (on a skeleton
// entity, a child of the glTF root) is the source of truth for which skeletons exist and how
// they bind to the scene; this manager owns the Vulkan resources (one compute pipeline; per-
// skeleton joint-DQ buffers; per-group vertex buffers + morph state) keyed by id, and step()
// discovers skeletons by viewing the `Skeleton` component, so any number of skeletons are
// supported. Groups and skeletons are registered at load time (before build); step() records
// one dispatch per group on the sim command buffer, before the soft sim, at the fixed sim rate.
// A skeleton death freezes its meshes in the last pose: the group buffers stay (the renderer
// still draws them) and are freed, one frame later, when the last aliasing mesh dies.
class Skin {
public:
    static constexpr int kMaxActiveMorphs = 256; // a Daz character's full stack is 190-222 targets, all nonzero
    static constexpr int kPoolChunk = 16;        // descriptor pool growth step for runtime skeletons

    void init(VkDevice dev, VkPhysicalDevice pdev);
    // allocate a skeleton's joint-DQ buffer (nJoints dual quaternions), keyed by the skeleton entity
    void addSkeleton(entt::entity e, int nJoints);
    // allocate a group's vertex buffers, linked to the skeleton's DQ buffer; returns the group id
    int addGroup(entt::entity e, const SkelGroupDesc& d);
    // a skinned prim entity aliases this group's posOut/nrmOut; the group's buffers are freed
    // once the skeleton is dead and the last linked mesh is gone
    void linkMesh(entt::entity mesh, int groupId);
    // the group's posOut/nrmOut as a MeshGpu; the renderer aliases these buffers (addSharedMesh)
    MeshGpu groupOut(int groupId) const;
    void build();
    // per fixed step, BEFORE stepSoft: view the ECS `Skeleton` components, resolve each skeleton's
    // joint DQs from the ECS, pack active morphs, and dispatch one compute pass per group
    void step(const entt::registry& reg, VkCommandBuffer cmd);
    // morph weights live per group; a name applies to every group of the skeleton that has it
    void setMorph(const entt::registry& reg, entt::entity skel, const std::string& name, float weight);
    float getMorph(const entt::registry& reg, entt::entity skel, const std::string& name);
    void resetMorphs(const entt::registry& reg, entt::entity skel);
    void destroySkeleton(entt::entity e);
    // register the destroy observers: a Skeleton teardown freezes the skeleton (its meshes keep
    // drawing the last pose); a mesh (Renderable) teardown unlinks it and may free the group
    void watch(entt::registry& reg);
    // free the group buffers queued by the previous frame (one in-flight frame may read them)
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
        bool dead = false; // frozen: not dispatched anymore (skeleton dead or last mesh gone)
        bool freed = false; // its buffers already released (flushFrees or shutdown)
        std::vector<float> morphW;                    // current weight per target
        std::vector<float> morphW0;                   // the file's static weights (startup + reset state)
        std::map<std::string, int> nameToTarget;
        Buf pos0, nrm0, joints, weights, deltas, activeMorphs, posOut, nrmOut;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    struct Skel {
        bool dead = false; // frozen: not stepped anymore (its meshes keep drawing the last pose)
        std::vector<int> groups; // this skeleton's group ids (the dispatch units)
        Buf jointDQ; // nJ x sizeof(DualQuat), host-visible (rewritten every step)
        std::vector<DualQuat> dqStaging;
    };

    void makeGroupSet(Group& g);
    void ensurePoolSlot(); // grow the descriptor pools in kPoolChunk steps when the pool is used up
    void stepSkeleton(const entt::registry& reg, const Skeleton& s, Skel& c);
    void packMorphs(Group& g);
    int meshRefs(int groupId) const; // live meshes aliasing the group
    void maybeFreeGroup(int groupId); // free (next frame) when frozen and unaliased
    void releaseGroupBufs(Group& g); // free the group's buffers (idempotent)
    std::unordered_map<entt::entity, int>::const_iterator skelFor(const entt::registry& reg, entt::entity skel) const;
    void onSkeletonDestroyed(entt::registry&, entt::entity e); // destroy-observer handlers
    void onMeshDestroyed(entt::registry&, entt::entity e);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    std::vector<Skel> m_skels; // keyed by skelGpu
    std::unordered_map<entt::entity, int> m_ent; // skeleton entity -> skelGpu
    std::unordered_map<entt::entity, int> m_meshGroup; // skinned mesh entity -> group id
    std::vector<int> m_freeGroups;                     // frozen groups awaiting the frame-start flush
    std::vector<Group> m_groups; // keyed by group id
    VkPipeline m_pipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_dsl = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_pools; // grown in kPoolChunk steps as runtime characters are added
    int m_poolCap = 0, m_poolUsed = 0;     // the pools.back() budget
    VkPipelineLayout m_pl = VK_NULL_HANDLE;
};
