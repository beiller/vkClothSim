// softsim.cpp
#include "sim/softsim.hpp"

#include "softbody_spv.hpp"
#include "vk/vkutil.hpp"
#include <cstring>

namespace {

// Matches the PhysP struct in softbody.comp (std430 flat scalars).
struct PhysParams {
    int nCaps;
    float dt, damping, gravity, friction, skin, fricMargin, tension, stiff, maxStep;
};
static_assert(sizeof(PhysParams) == 40);

// Max vertex displacement per sub-step (m): the hard anti-divergence guard in the Verlet
// pass. The cloth's smallest rest length is kClothSpan/(kCW-1) ~ 0.13 m, so 0.05 m keeps a
// single step well under half a joint even under the most extreme parameter sets.
constexpr float SOFT_MAX_STEP = 0.05f;

// The compute DSL's 10 bindings (shared by both bodies' descriptor sets):
// 0 posA, 1 posB, 2 prev, 3 capsules, 4 entries, 5 entryStart, 6 physParams, 7 sub0,
// 8 contactN, 9 contactL.
std::vector<VkDescriptorSetLayoutBinding> softBinds() {
    std::vector<VkDescriptorSetLayoutBinding> binds(10);
    for (uint32_t i = 0; i < binds.size(); ++i) {
        binds[i].binding = i;
        binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binds[i].descriptorCount = 1;
        binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return binds;
}

// A buffer-memory barrier over `buf` with the sim's standard access (SHADER_READ|WRITE ->
// SHADER_READ|WRITE). `dst` may be narrowed (e.g. the trailing sub0 READ).
VkBufferMemoryBarrier bufBarrier(VkBuffer buf,
                                 VkAccessFlags dst = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT) {
    VkBufferMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = buf;
    b.offset = 0;
    b.size = VK_WHOLE_SIZE;
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = dst;
    return b;
}

// A pipeline barrier over `n` buffer barriers, from stage `src` to stage `dst`.
void stageBarrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                  const VkBufferMemoryBarrier* bmbs, uint32_t n) {
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, n, bmbs, 0, nullptr);
}

} // namespace

void SoftSim::init(VkDevice dev, VkPhysicalDevice pdev, const sim::SoftBody& cloth, const sim::SoftBody& ball,
                   int nCaps) {
    m_dev = dev;
    m_pdev = pdev;
    m_nCaps = nCaps;

    // the shared physics params (small SSBO, rewritten each frame)
    vkMakeBuffer(dev, pdev, m_physParams, m_physParamsMem, 40, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    // the Jolt capsule colliders (shared with the renderer's instanced draw)
    vkMakeBuffer(dev, pdev, m_capsInstances, m_capsInstancesMem, 48 * (VkDeviceSize)nCaps,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    // compute DSL + pool (7 buffers x 2 bodies) + pipeline
    const auto binds = softBinds();
    vkMakeDslPool(dev, binds, /*maxSets=*/2, m_softDsl, m_softPool);
    m_softPl = vkMakePipelineLayout(dev, m_softDsl);
    VkShaderModule cm = vkMakeModule(dev, softbody_spv, softbody_spv_len / 4);
    VkPipelineShaderStageCreateInfo cs{};
    cs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cs.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cs.module = cm;
    cs.pName = "main";
    VkComputePipelineCreateInfo cpc{};
    cpc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpc.stage = cs;
    cpc.layout = m_softPl;
    VK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpc, nullptr, &m_softPipe));
    vkDestroyShaderModule(dev, cm, nullptr);

    // the two bodies: their sim buffers + compute descriptor sets
    m_body[kCloth].n = cloth.numVertices();
    buildBodySimBuffers(m_body[kCloth], cloth);
    m_body[kBall].n = ball.numVertices();
    buildBodySimBuffers(m_body[kBall], ball);
}

void SoftSim::buildBodySimBuffers(GpuBody& b, const sim::SoftBody& body) {
    VkDevice dev = m_dev;
    const int n = b.n;
    // the joint entries: each constraint (a,b) adds an entry to a's list (neighbor b) and to
    // b's list (neighbor a) — the shader relaxes each vertex against its own entries. The
    // layout mirrors the shader's `struct Entry { int j; float rest; float k; int pad; }`
    // (16 B) — `j` MUST be an int in memory (the shader reads it as an int).
    struct EntryBuf {
        int j;
        float rest;
        float k;
        int pad;
    };
    std::vector<std::vector<EntryBuf>> lists(n);
    for (const auto& c : body.cons) {
        lists[c.a].push_back({c.b, c.rest, c.k, 0});
        lists[c.b].push_back({c.a, c.rest, c.k, 0});
    }
    std::vector<int> start(n + 1, 0);
    for (int i = 0; i < n; ++i)
        start[i + 1] = start[i] + (int)lists[i].size();
    b.nEntries = start[n];
    std::vector<EntryBuf> entries(b.nEntries);
    for (int i = 0; i < n; ++i)
        for (size_t e = 0; e < lists[i].size(); ++e)
            entries[start[i] + e] = lists[i][e];
    // the render Vtx buffers (48 bytes = pos+nrm+col). .pos/.nrm are stepped by the sim; .col
    // is static. BOTH posA and posB get the full init Vtx: either can be the "current" buffer
    // the render reads (the .col must be valid in both; .nrm is recomputed each frame).
    std::vector<float> initVtx((size_t)12 * n);
    for (int i = 0; i < n; ++i) {
        const float* p = body.posPtr(i);
        const float* nr = body.nrmPtr(i);
        const float* c = body.colPtr(i);
        initVtx[12 * i + 0] = p[0];
        initVtx[12 * i + 1] = p[1];
        initVtx[12 * i + 2] = p[2];
        initVtx[12 * i + 3] = 0.0f; // pos (16 B)
        initVtx[12 * i + 4] = nr[0];
        initVtx[12 * i + 5] = nr[1];
        initVtx[12 * i + 6] = nr[2];
        initVtx[12 * i + 7] = 0.0f; // nrm (16 B)
        initVtx[12 * i + 8] = c[0];
        initVtx[12 * i + 9] = c[1];
        initVtx[12 * i + 10] = c[2];
        initVtx[12 * i + 11] = 0.0f; // col (16 B)
    }
    // prev is the 16-byte-stride pos (zero velocity) — a 16-byte view of the init Vtx.
    std::vector<float> initPos((size_t)4 * n);
    for (int i = 0; i < n; ++i) {
        initPos[4 * i + 0] = initVtx[12 * i + 0];
        initPos[4 * i + 1] = initVtx[12 * i + 1];
        initPos[4 * i + 2] = initVtx[12 * i + 2];
        initPos[4 * i + 3] = 0.0f;
    }
    vkMakeBuffer(dev, m_pdev, b.posA, b.posAMem, (VkDeviceSize)48 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 initVtx.data());
    vkMakeBuffer(dev, m_pdev, b.posB, b.posBMem, (VkDeviceSize)48 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 initVtx.data());
    vkMakeBuffer(dev, m_pdev, b.prev, b.prevMem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 initPos.data()); // prev = pos (zero velocity)
    vkMakeBuffer(dev, m_pdev, b.sub0, b.sub0Mem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 initPos.data()); // sub0 = the sub-step start position (for friction)
    vkMakeBuffer(dev, m_pdev, b.contactN, b.contactNMem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 nullptr);
    vkMakeBuffer(dev, m_pdev, b.contactL, b.contactLMem, (VkDeviceSize)4 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 nullptr);
    b.initVtx = std::move(initVtx); // keep a CPU copy (pos+nrm+col) for reset
    b.initPos = std::move(initPos); // keep the 16-byte pos copy for prev + reset
    vkMakeBuffer(dev, m_pdev, b.entries, b.entriesMem, (VkDeviceSize)16 * b.nEntries,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, entries.data());
    vkMakeBuffer(dev, m_pdev, b.entryStart, b.entryStartMem, (VkDeviceSize)4 * (n + 1),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, start.data());
    // the compute descriptor set (this body's buffers + the shared capsules + phys params)
    std::vector<VkDescriptorBufferInfo> bi = {
        {b.posA, 0, (VkDeviceSize)48 * n},
        {b.posB, 0, (VkDeviceSize)48 * n},
        {b.prev, 0, (VkDeviceSize)16 * n},
        {m_capsInstances, 0, (VkDeviceSize)48 * m_nCaps},
        {b.entries, 0, (VkDeviceSize)16 * b.nEntries},
        {b.entryStart, 0, (VkDeviceSize)4 * (n + 1)},
        {m_physParams, 0, 40},
        {b.sub0, 0, (VkDeviceSize)16 * n},
        {b.contactN, 0, (VkDeviceSize)16 * n},
        {b.contactL, 0, (VkDeviceSize)4 * n},
    };
    vkMakeSet(dev, m_softPool, m_softDsl, softBinds(), b.simSet, bi);
}

void SoftSim::recordDispatch(VkCommandBuffer cmd, GpuBody& b, int mode) {
    VkBuffer rd = b.inA ? b.posA : b.posB;
    // ALL_COMMANDS src stage: the CURRENT pos buffer may have been read by the previous
    // frame's render pass, so the sim's next access must wait for both compute writes AND
    // graphics reads.
    VkBufferMemoryBarrier bmb[4] = {bufBarrier(rd), bufBarrier(b.prev), bufBarrier(b.contactN), bufBarrier(b.contactL)};
    stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, bmb, 4);
    dispatch(cmd, b, mode);
    b.inA = !b.inA; // the write went to the other buffer
}

void SoftSim::dispatch(VkCommandBuffer cmd, GpuBody& b, int mode) {
    int pc[4] = {mode, b.inA ? 1 : 0, b.n, 0}; // mode, readFromA, n, pinned(unused)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPl, 0, 1, &b.simSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_softPl, VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)sizeof(pc), pc);
    vkCmdDispatch(cmd, (b.n + 63) / 64, 1, 1);
}

// The mode-3 normal pass: read the CURRENT buffer's .pos, write .nrm into the SAME buffer.
// No inA flip (it reads and writes the same buffer), so the buffer the render reads holds the
// fresh pos + nrm.
void SoftSim::recordNorm(VkCommandBuffer cmd, GpuBody& b) {
    VkBuffer rd = b.inA ? b.posA : b.posB;
    VkBufferMemoryBarrier bmb = bufBarrier(rd);
    stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, &bmb, 1);
    dispatch(cmd, b, 3); // no inA flip: it writes .nrm to the buffer it reads
}

void SoftSim::recordSub0(VkCommandBuffer cmd, GpuBody& b) {
    // Mode 4 reads the CURRENT position buffer and writes this thread's `sub0` slot. No `inA`
    // flip (the ping-pong position is untouched). Barrier the CURRENT pos (read) AND `sub0`
    // (write-after-write from the previous sub-step's mode 4) before the dispatch.
    VkBuffer rd = b.inA ? b.posA : b.posB;
    VkBufferMemoryBarrier bmb[4] = {bufBarrier(rd), bufBarrier(b.sub0), bufBarrier(b.contactN), bufBarrier(b.contactL)};
    stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, bmb, 4);
    dispatch(cmd, b, 4); // no inA flip: the ping-pong position is untouched
    VkBufferMemoryBarrier sb[3] = {
        bufBarrier(b.sub0, VK_ACCESS_SHADER_READ_BIT),
        bufBarrier(b.contactN, VK_ACCESS_SHADER_READ_BIT),
        bufBarrier(b.contactL, VK_ACCESS_SHADER_READ_BIT),
    };
    stageBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, sb, 3);
}

void SoftSim::recordFinalize(VkCommandBuffer cmd, GpuBody& b) {
    // Mode 5 reads the CURRENT solved position + `sub0`, and writes `prev` (the velocity
    // update). No position ping-pong flip.
    VkBuffer rd = b.inA ? b.posA : b.posB;
    VkBufferMemoryBarrier bmb[5] = {
        bufBarrier(rd, VK_ACCESS_SHADER_READ_BIT),
        bufBarrier(b.sub0, VK_ACCESS_SHADER_READ_BIT),
        bufBarrier(b.contactN, VK_ACCESS_SHADER_READ_BIT),
        bufBarrier(b.contactL, VK_ACCESS_SHADER_READ_BIT),
        bufBarrier(b.prev),
    };
    stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, bmb, 5);
    dispatch(cmd, b, 5);
}

void SoftSim::recordBody(VkCommandBuffer cmd, GpuBody& b, int iters, int pinned) {
    if (pinned)
        return;
    for (int s = 0; s < sim::kSubsteps; ++s) {
        recordSub0(cmd, b);        // x0 = the pre-predict position (for the velocity update)
        recordDispatch(cmd, b, 0); // Verlet predict (gravity + velocity, once per sub-step)
        for (int k = 0; k < iters; ++k) {
            recordDispatch(cmd, b, 2); // collide: XPBD one-way capsule/ground projection
            recordDispatch(cmd, b, 1); // relax: XPBD joint distance constraints (Jacobi)
        }
        recordDispatch(cmd, b, 2); // final collide: leave the rendered vertex out of capsules
        recordFinalize(cmd, b);    // velocity update + friction (writes `prev`)
    }
    recordNorm(cmd, b); // deformed normals (for the render)
}

void SoftSim::record(VkCommandBuffer cmd, const SimParams& p, int clothPinned) {
    PhysParams phys;
    phys.nCaps = m_nCaps;
    phys.dt = kFrameDt / sim::kSubsteps;
    phys.damping = p.damping;
    phys.gravity = sim::kGravity * p.mass;
    phys.friction = p.friction;
    phys.skin = 0.01f;
    phys.fricMargin = 0.2f; // the contact band in which slip-based friction is applied
    phys.tension = p.tension;
    phys.stiff = p.stiffness; // XPBD constraint stiffness scale (UI)
    phys.maxStep = SOFT_MAX_STEP;
    void* up;
    VK(vkMapMemory(m_dev, m_physParamsMem, 0, 40, 0, &up));
    std::memcpy(up, &phys, 40);
    vkUnmapMemory(m_dev, m_physParamsMem);
    // the XPBD solver passes per sub-step (N collide + N relax) = the UI `passes` slider. The
    // cloth is held (pinned) while it is; the ball is always free.
    const int pinned[kCount] = {clothPinned, 0};
    for (int i = 0; i < kCount; ++i)
        recordBody(cmd, m_body[i], p.passes, pinned[i]);
    // Make the sim's final writes to each body's CURRENT Vtx buffer visible to the render
    // pass (the renderer reads .pos/.nrm from these buffers in the vertex shader).
    for (GpuBody& b : m_body) {
        // Conservative compute->graphics barrier: ALL_COMMANDS covers the sim's compute writes
        // AND any prior access; the render pass reads .pos/.nrm in the vertex shader.
        VkBufferMemoryBarrier bmb = bufBarrier(b.inA ? b.posA : b.posB);
        stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, &bmb, 1);
    }
}

void SoftSim::resetBody(GpuBody& b) {
    // restore posA + posB to the initial Vtx (pos+nrm+col) and prev to the initial pos.
    void* pa;
    VK(vkMapMemory(m_dev, b.posAMem, 0, (VkDeviceSize)48 * b.n, 0, &pa));
    std::memcpy(pa, b.initVtx.data(), (size_t)48 * b.n);
    vkUnmapMemory(m_dev, b.posAMem);
    void* pb;
    VK(vkMapMemory(m_dev, b.posBMem, 0, (VkDeviceSize)48 * b.n, 0, &pb));
    std::memcpy(pb, b.initVtx.data(), (size_t)48 * b.n);
    vkUnmapMemory(m_dev, b.posBMem);
    void* pv;
    VK(vkMapMemory(m_dev, b.prevMem, 0, (VkDeviceSize)16 * b.n, 0, &pv));
    std::memcpy(pv, b.initPos.data(), (size_t)16 * b.n);
    vkUnmapMemory(m_dev, b.prevMem);
    void* cn;
    VK(vkMapMemory(m_dev, b.contactNMem, 0, (VkDeviceSize)16 * b.n, 0, &cn));
    std::memset(cn, 0, (size_t)16 * b.n);
    vkUnmapMemory(m_dev, b.contactNMem);
    void* cl;
    VK(vkMapMemory(m_dev, b.contactLMem, 0, (VkDeviceSize)4 * b.n, 0, &cl));
    std::memset(cl, 0, (size_t)4 * b.n);
    vkUnmapMemory(m_dev, b.contactLMem);
    b.inA = true;
}

void SoftSim::resetSoftBodies() {
    for (auto& b : m_body)
        resetBody(b);
}

void SoftSim::resetBall() {
    resetBody(m_body[kBall]);
}

void SoftSim::uploadCapsules(const std::vector<CapsuleGPU>& instances) {
    void* p;
    VK(vkMapMemory(m_dev, m_capsInstancesMem, 0, 48 * instances.size(), 0, &p));
    std::memcpy(p, instances.data(), 48 * instances.size());
    vkUnmapMemory(m_dev, m_capsInstancesMem);
}

void SoftSim::shutdown() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    auto db = [this](VkBuffer buf, VkDeviceMemory mem) {
        if (buf) {
            vkDestroyBuffer(m_dev, buf, nullptr);
            vkFreeMemory(m_dev, mem, nullptr);
        }
    };
    for (auto& b : m_body) {
        db(b.posA, b.posAMem);
        db(b.posB, b.posBMem);
        db(b.prev, b.prevMem);
        db(b.sub0, b.sub0Mem);
        db(b.contactN, b.contactNMem);
        db(b.contactL, b.contactLMem);
        db(b.entries, b.entriesMem);
        db(b.entryStart, b.entryStartMem);
    }
    if (m_softPool)
        vkDestroyDescriptorPool(m_dev, m_softPool, nullptr);
    if (m_softDsl)
        vkDestroyDescriptorSetLayout(m_dev, m_softDsl, nullptr);
    if (m_softPl)
        vkDestroyPipelineLayout(m_dev, m_softPl, nullptr);
    if (m_softPipe)
        vkDestroyPipeline(m_dev, m_softPipe, nullptr);
    db(m_physParams, m_physParamsMem);
    db(m_capsInstances, m_capsInstancesMem);
}
