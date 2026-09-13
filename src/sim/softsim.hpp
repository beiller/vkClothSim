// softsim.hpp
// The GPU soft-body sim — the "cloth sim" concern, kept out of the renderer. Owns the ONE
// general compute shader (shaders/softbody.comp) + the per-body sim state (ping-pong
// positions + the joint entries) + the Jolt-capsule colliders + the shared physics params.
// It steps the cloth and the ball with the SAME pipeline: each body is just (vertices +
// two-point distance joints), collided against the capsules + the ground.
//
// Deliberately decoupled from the renderer (Vulkan graphics) and the scene: it only records
// compute dispatches into a caller-provided command buffer and exposes the resulting
// position buffers + the capsule buffer for the renderer to read. The scene seeds the body
// data (sim::SoftBody); the renderer draws it.
#pragma once
#include <vulkan/vulkan.h>

#include "app/params.hpp" // SimParams
#include "app/rigid.hpp"  // CapsuleGPU
#include "sim/sim.hpp"
#include <cstdint>
#include <vector>

class SoftSim {
public:
    // The two soft bodies, stepped by the same compute pipeline.
    enum Body : std::uint8_t { kCloth = 0, kBall = 1, kCount = 2 };

    // `cloth`/`ball` are the bodies' initial state (vertices + joints); `nCaps` sizes the
    // capsule collider buffer. `dev`/`pdev` are the Vulkan handles (no VkApp dependency).
    void init(VkDevice dev, VkPhysicalDevice pdev, const sim::SoftBody& cloth, const sim::SoftBody& ball, int nCaps);
    void shutdown();

    // Upload the Jolt capsule transforms (CPU -> GPU). The sim reads them as colliders; the
    // renderer reads the SAME buffer (capsulesBuffer) for the instanced capsule draw.
    void uploadCapsules(const std::vector<CapsuleGPU>& instances);

    // Record one frame of the sim (both bodies: substeps x {predict + (collide + relax) x N +
    // collide + velocity update}) into `cmd` (no submit). Call this BEFORE the render reads the
    // position buffers.
    void record(VkCommandBuffer cmd, const SimParams& p, int clothPinned);

    void resetSoftBodies(); // both bodies (R key / the "reset" button)
    void resetBall();       // just the ball (the "reset ball" button)

    // The position buffer that currently holds body `body`'s live state (the render reads it).
    VkBuffer posBuffer(Body body) const { return m_body[body].inA ? m_body[body].posA : m_body[body].posB; }
    int vertexCount(Body body) const { return m_body[body].n; }
    // The capsule collider buffer (shared with the renderer's instanced draw) + its count.
    VkBuffer capsulesBuffer() const { return m_capsInstances; }
    int capsuleCount() const { return m_nCaps; }

private:
    // One body's sim state: the ping-pong positions (posA/posB) + prev + sub0 (the pre-predict
    // x0 for the PBD velocity update) + the joint entries. `inA` tracks which position buffer
    // holds the current state (the sim flips it per position-writing dispatch).
    struct GpuBody {
        VkBuffer posA = VK_NULL_HANDLE, posB = VK_NULL_HANDLE, prev = VK_NULL_HANDLE, sub0 = VK_NULL_HANDLE;
        VkBuffer contactN = VK_NULL_HANDLE, contactL = VK_NULL_HANDLE;
        VkBuffer entries = VK_NULL_HANDLE, entryStart = VK_NULL_HANDLE;
        VkDeviceMemory posAMem = VK_NULL_HANDLE, posBMem = VK_NULL_HANDLE, prevMem = VK_NULL_HANDLE,
                       sub0Mem = VK_NULL_HANDLE;
        VkDeviceMemory contactNMem = VK_NULL_HANDLE, contactLMem = VK_NULL_HANDLE;
        VkDeviceMemory entriesMem = VK_NULL_HANDLE, entryStartMem = VK_NULL_HANDLE;
        VkDescriptorSet simSet = VK_NULL_HANDLE;
        int n = 0;
        int nEntries = 0;
        bool inA = true;
        std::vector<float> initPos; // 16-byte stride (pos), kept for prev + reset
        std::vector<float> initVtx; // 48-byte stride (pos+nrm+col), kept for reset
    };
    // Create one body's sim buffers (posA/posB/prev + the joint entries) from its initial
    // vertices + constraints, plus its compute descriptor set.
    void buildBodySimBuffers(GpuBody& b, const sim::SoftBody& body);
    // Bind the pipeline + descriptors, push the mode push-constants, and dispatch (no barriers,
    // no inA flip — the caller owns those).
    void dispatch(VkCommandBuffer cmd, GpuBody& b, int mode);
    // Record ONE soft-body dispatch (barriers + push constants + dispatch) and flip inA (the
    // write went to the other buffer).
    void recordDispatch(VkCommandBuffer cmd, GpuBody& b, int mode);
    // Record the mode-3 normal pass for one body: recompute .nrm from the joints into the
    // CURRENT buffer (no inA flip — it writes to the buffer it reads, so the render reads the
    // fresh pos + nrm from one buffer).
    void recordNorm(VkCommandBuffer cmd, GpuBody& b);
    // Record the mode-4 sub-step-start pass: copy the CURRENT position into `sub0` (the
    // pre-predict x0) before the Verlet predict (no inA flip).
    void recordSub0(VkCommandBuffer cmd, GpuBody& b);
    // Record the mode-5 velocity-update pass: derive v from (solvedPos - sub0), apply contact
    // friction, and write `prev` (no inA flip).
    void recordFinalize(VkCommandBuffer cmd, GpuBody& b);
    // Record one body's full frame of sim (substeps x {predict + (collide + relax) x iters +
    // collide + finalize} + a final normal pass); skip if pinned. The relax is Jacobi (the
    // cloth's joint graph is not bipartite). `iters` = the XPBD solver passes (UI `passes`).
    void recordBody(VkCommandBuffer cmd, GpuBody& b, int iters, int pinned);
    // Re-upload one body's initial state to the GPU (posA + prev = initial, inA = true).
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
