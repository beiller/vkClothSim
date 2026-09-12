// renderer.hpp
// The renderer: the GPU side of the app. Owns the per-mesh pipelines + buffers (the
// full-screen background, the instanced capsules + ground, and the two soft bodies — the
// cloth sheet + the ball), the soft-body COMPUTE sim (per-frame dispatch, double-buffered,
// race-free), the per-frame uploads, and the draws.
//
// The soft bodies (cloth + ball) are stepped ON THE GPU by ONE general compute shader
// (shaders/softbody.comp): it takes each body's vertices + joints (two-point distance
// constraints) + the Jolt capsules (colliders) and does Verlet + joint relaxation + one-way
// collision. The renderer reads the resulting positions back for the render (same buffer).
#pragma once
#include <vulkan/vulkan.h>
#include <imgui.h>          // ImDrawData (the optional overlay, drawn last)
#include <vector>
#include "math.hpp"
#include "sim/sim.hpp"
#include "app/rigid.hpp"
#include "app/params.hpp"

class VkApp;
class Scene;

class Renderer {
public:
    void init(VkApp& app, const Scene& scene, const Mat4& viewProj);
    void shutdown();

    void setViewProj(const Mat4& vp) { m_vp = vp; }
    // Upload the Jolt capsules (CPU -> GPU): used by BOTH the instanced render and the sim.
    void uploadCapsules(const std::vector<CapsuleGPU>& instances);
    // Record the soft-body sim (cloth + ball) dispatches into `cmd` (no submit).
    void recordSoftSim(VkCommandBuffer cmd, const SimParams& p, int clothPinned);
    // --shot helper: record + submit + wait ONE sim step (no render).
    void stepSoftFrame(VkApp& app, const SimParams& p, int clothPinned, float relaxScale = 1.0f);
    // Read back a body's current positions ("cloth"/"ball") for --shot diagnostics.
    void readbackBody(VkApp& app, const char* name, std::vector<float>& out);
    // Re-upload the initial state to the GPU (used on reset: the sim state lives on the GPU).
    void resetSoftBodies();   // both bodies (R key / reset button)
    void resetBall();         // just the ball ("reset ball" button)
    // Record + submit the render into framebuffer `fb`. If `doSim`, the soft-body sim is
    // recorded first (live mode); the --shot final render passes doSim=false.
    void draw(VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui,
              bool doSim, const SimParams& p, int clothPinned);

private:
    // One indexed render mesh: the pos SSBO (soft bodies) or vertex buffer (capsules) + the
    // index buffer + the 80-byte UBO + the pipeline + the render descriptor set.
    struct Mesh {
        VkBuffer vbuf = VK_NULL_HANDLE;   VkDeviceMemory vbmem = VK_NULL_HANDLE;
        VkBuffer sbuf = VK_NULL_HANDLE;   VkDeviceMemory sbmem = VK_NULL_HANDLE;
        VkBuffer ibuf = VK_NULL_HANDLE;   VkDeviceMemory ibmem = VK_NULL_HANDLE;
        VkBuffer ubuf = VK_NULL_HANDLE;   VkDeviceMemory ubmem = VK_NULL_HANDLE;
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkPipelineLayout pl = VK_NULL_HANDLE;
        VkPipeline pipe = VK_NULL_HANDLE;
        uint32_t idxCount = 0;
    };

    // One GPU soft body (the cloth or the ball): the sim buffers (ping-pong positions + prev
    // + the joint entries) + the compute descriptor set + the render mesh. `inA` tracks which
    // position buffer holds the current state (the sim flips it per dispatch).
    struct GpuBody {
        VkBuffer posA = VK_NULL_HANDLE;   VkDeviceMemory posAMem = VK_NULL_HANDLE;
        VkBuffer posB = VK_NULL_HANDLE;   VkDeviceMemory posBMem = VK_NULL_HANDLE;
        VkBuffer prev = VK_NULL_HANDLE;   VkDeviceMemory prevMem = VK_NULL_HANDLE;
        VkBuffer entries = VK_NULL_HANDLE; VkDeviceMemory entriesMem = VK_NULL_HANDLE;
        VkBuffer entryStart = VK_NULL_HANDLE; VkDeviceMemory entryStartMem = VK_NULL_HANDLE;
        VkDescriptorSet simSet = VK_NULL_HANDLE;
        Mesh mesh;                         // render (binding 0 = the current pos buffer)
        int n = 0;                         // vertex count
        int nEntries = 0;
        bool inA = true;
        std::vector<float> initPos;        // the initial state (16-byte stride), for reset
    };

    void initBackground();
    void initCapsules();
    void initSoftPipeline();               // the compute pipeline + dsl + shared phys params
    void initSoftBodies(const Scene& scene);   // build m_cloth + m_ball (sim buffers + render)
    // Create one body's sim buffers (posA/posB/prev + the joint entries) from its initial
    // vertices + constraints, plus its compute descriptor set.
    void buildBodySimBuffers(GpuBody& b, const sim::SoftBody& body);
    // The cloth + ball share the render descriptor layout (0: pos SSBO, 1: UBO).
    void makePosUboDescriptorSet(VkDevice dev, Mesh& m, VkDeviceSize posRange);
    void uploadGridUbo(Mesh& m);
    // Point a body's render descriptor at its current position buffer (posA or posB).
    void updateRenderSet(GpuBody& b);
    // Record ONE soft-body compute dispatch (barriers + push constants + dispatch) and flip
    // inA (the write went to the other buffer).
    void recordDispatch(VkCommandBuffer cmd, GpuBody& b, int mode);
    // Record one body's full frame of sim (substeps x {Verlet + relax + collide}); skip if
    // pinned. The relax is Jacobi (the cloth's joint graph is not bipartite).
    void recordBody(VkCommandBuffer cmd, GpuBody& b, int relaxIters, int pinned);
    // Re-upload one body's initial state to the GPU (posA + prev = initial, inA = true).
    void resetBody(GpuBody& b);
    static void drawMesh(VkCommandBuffer cmd, const Mesh& m, VkBuffer* vbuf);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    VkRenderPass m_rp = VK_NULL_HANDLE;
    Mat4 m_vp{};

    // background (full-screen triangle; its own VBO + color UBO)
    VkBuffer m_bgUbuf = VK_NULL_HANDLE;   VkDeviceMemory m_bgUmem = VK_NULL_HANDLE;
    VkBuffer m_bgVbuf = VK_NULL_HANDLE;   VkDeviceMemory m_bgVmem = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_bgDsl = VK_NULL_HANDLE;
    VkDescriptorPool m_bgPool = VK_NULL_HANDLE;
    VkDescriptorSet m_bgSet = VK_NULL_HANDLE;
    VkPipelineLayout m_bgPl = VK_NULL_HANDLE;
    VkPipeline m_bgPipe = VK_NULL_HANDLE;

    // the instanced capsule + ground mesh
    Mesh m_caps;
    VkBuffer m_capsInstances = VK_NULL_HANDLE;   VkDeviceMemory m_capsInstancesMem = VK_NULL_HANDLE;
    int m_nCaps = 0;
    int m_clothW = 0, m_clothH = 0;

    // the two GPU soft bodies + the shared compute sim
    GpuBody m_cloth, m_ball;
    VkPipeline m_softPipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_softDsl = VK_NULL_HANDLE;
    VkDescriptorPool m_softPool = VK_NULL_HANDLE;
    VkPipelineLayout m_softPl = VK_NULL_HANDLE;
    VkBuffer m_physParams = VK_NULL_HANDLE;   VkDeviceMemory m_physParamsMem = VK_NULL_HANDLE;
    float m_relaxScale = 1.0f;   // the relax scale (softer constraints -> more drape); 1.0 = full
};
