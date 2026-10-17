#pragma once
#include <vulkan/vulkan.h>

#include "capsule.hpp"
#include "ecs.hpp"
#include "gpuverts.hpp"
#include "mesh.hpp"
#include "sim/params.hpp"
#include "sim/phys.hpp"
#include "sim/sim.hpp"
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Engine;

class Soft {
public:
    void init(VkDevice dev, VkPhysicalDevice pdev);
    // first-sight allocation: a SoftBodyData component without a GPU body reads its vertex data
    // from the Renderable's geometry, lifts it to world space at the entity's pose, uploads it,
    // and allocates the sim buffers
    void project(Engine& w);
    // scene ground: the soft-body floor plane (top face of the ground box); -inf = no floor
    void setFloor(float y);
    // upload the live capsule list: every body's referenced colliders composed to world
    // (body world ∘ collider local); call after resolveWorldTransforms, before record
    void syncCapsules(Engine& w);
    // live capsule list: the caller fills the staging array (any order, capped) then uploads;
    // identity-free, so destroyed capsules just fall out next step
    static constexpr int kMaxCapsules = 16384;
    static constexpr int kPoolChunk = 16; // descriptor pool growth step for runtime bodies
    CapsuleGPU* caps() { return m_caps.data(); }
    void uploadCapsules(int n);
    // the sim's own GPU buffers (pos/nrm stay the renderer's mesh); keyed by the body entity
    void addSoftBody(entt::entity e, const Mesh& mesh, const std::vector<sim::Constraint>& cons, const MeshGpu& rw);
    void build();
    void shutdown();
    void setPinned(entt::entity e, int pinned);
    void setParams(entt::entity e, const SimParams& params, int steps);
    void record(VkCommandBuffer cmd);
    // register the destroy observer: a SoftBodyData teardown retires the GPU body and queues
    // its buffers for the next frame-start flush (the previous frame may still be reading them)
    void watch(entt::registry& reg);
    void flushFrees();

private:
    struct GpuBody {
        int pinned = 0;
        SimParams params;
        int steps = kDefaultSteps;
        bool dead = false; // retired (entity destroyed): not recorded anymore
        bool freed = false; // its own buffers already released (flushFrees or shutdown)
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
    void ensurePoolSlot(); // grow the descriptor pools in kPoolChunk steps when the pool is used up
    void freeBody(GpuBody& b); // the sim's own buffers (pos/nrm belong to the renderer's mesh)
    void onBodyDestroyed(entt::registry&, entt::entity e); // destroy-observer handler
    static int modeBarriers(const GpuBody& b, int mode, VkBufferMemoryBarrier* out);
    void dispatch(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset);
    void recordMode(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset);
    void recordBody(VkCommandBuffer cmd, const GpuBody& b, int iters, int pinned);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    std::vector<CapsuleGPU> m_caps; // per-step staging for m_capsInstances
    int m_nCaps = 0;
    std::vector<GpuBody> m_body;
    std::unordered_map<entt::entity, int> m_ent; // body entity -> m_body index
    std::unordered_set<entt::entity> m_warned;   // entities already warned about in project()
    std::vector<int> m_free;                     // retired bodies awaiting the frame-start flush
    VkPipeline m_softPipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_softDsl = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_softPools; // grown in kPoolChunk steps as runtime bodies are added
    int m_softPoolCap = 0, m_softPoolUsed = 0; // the pools.back() budget
    VkPipelineLayout m_softPl = VK_NULL_HANDLE;
    VkBuffer m_capsInstances = VK_NULL_HANDLE;
    VkDeviceMemory m_capsInstancesMem = VK_NULL_HANDLE;
    float m_floorY = -std::numeric_limits<float>::infinity();
};
