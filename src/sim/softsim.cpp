// softsim.cpp
#include "sim/softsim.hpp"

#include "softbody_spv.hpp"
#include "vk/vkutil.hpp"
#include <cstring>

namespace {

struct PhysParams {
    int nCaps;
    float dt, damping, gravity, friction, skin, fricMargin, tension, stiff, maxStep;
};
static_assert(sizeof(PhysParams) == 40);

constexpr float SOFT_MAX_STEP = 0.05f;

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

    vkMakeBuffer(dev, pdev, m_physParams, m_physParamsMem, 40, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    vkMakeBuffer(dev, pdev, m_capsInstances, m_capsInstancesMem, 48 * (VkDeviceSize)nCaps,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
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

    m_body[kCloth].n = cloth.numVertices();
    buildBodySimBuffers(m_body[kCloth], cloth);
    m_body[kBall].n = ball.numVertices();
    buildBodySimBuffers(m_body[kBall], ball);
}

void SoftSim::buildBodySimBuffers(GpuBody& b, const sim::SoftBody& body) {
    VkDevice dev = m_dev;
    const int n = b.n;
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

    std::vector<float> initVtx((size_t)12 * n);
    for (int i = 0; i < n; ++i) {
        const float* p = body.posPtr(i);
        const float* nr = body.nrmPtr(i);
        const float* c = body.colPtr(i);
        initVtx[12 * i + 0] = p[0];
        initVtx[12 * i + 1] = p[1];
        initVtx[12 * i + 2] = p[2];
        initVtx[12 * i + 3] = 0.0f;
        initVtx[12 * i + 4] = nr[0];
        initVtx[12 * i + 5] = nr[1];
        initVtx[12 * i + 6] = nr[2];
        initVtx[12 * i + 7] = 0.0f;
        initVtx[12 * i + 8] = c[0];
        initVtx[12 * i + 9] = c[1];
        initVtx[12 * i + 10] = c[2];
        initVtx[12 * i + 11] = 0.0f;
    }
    std::vector<float> initPos((size_t)4 * n);
    for (int i = 0; i < n; ++i) {
        initPos[4 * i + 0] = initVtx[12 * i + 0];
        initPos[4 * i + 1] = initVtx[12 * i + 1];
        initPos[4 * i + 2] = initVtx[12 * i + 2];
        initPos[4 * i + 3] = 0.0f;
    }
    b.colorCount = body.colorCount;
    b.colorStart = body.colorStart;
    vkMakeBuffer(dev, m_pdev, b.vtx, b.vtxMem, (VkDeviceSize)48 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 initVtx.data());
    vkMakeBuffer(dev, m_pdev, b.prev, b.prevMem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 initPos.data());
    vkMakeBuffer(dev, m_pdev, b.sub0, b.sub0Mem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 initPos.data());
    vkMakeBuffer(dev, m_pdev, b.contactN, b.contactNMem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 nullptr);
    vkMakeBuffer(dev, m_pdev, b.contactL, b.contactLMem, (VkDeviceSize)4 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 nullptr);
    b.initVtx = std::move(initVtx);
    b.initPos = std::move(initPos);
    vkMakeBuffer(dev, m_pdev, b.entries, b.entriesMem, (VkDeviceSize)16 * b.nEntries,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, entries.data());
    vkMakeBuffer(dev, m_pdev, b.entryStart, b.entryStartMem, (VkDeviceSize)4 * (n + 1),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, start.data());
    vkMakeBuffer(dev, m_pdev, b.colorVerts, b.colorVertsMem, (VkDeviceSize)4 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 body.colorVerts.data());

    std::vector<VkDescriptorBufferInfo> bi = {
        {b.vtx, 0, (VkDeviceSize)48 * n},
        {b.prev, 0, (VkDeviceSize)16 * n},
        {m_capsInstances, 0, (VkDeviceSize)48 * m_nCaps},
        {b.entries, 0, (VkDeviceSize)16 * b.nEntries},
        {b.entryStart, 0, (VkDeviceSize)4 * (n + 1)},
        {b.colorVerts, 0, (VkDeviceSize)4 * n},
        {m_physParams, 0, 40},
        {b.sub0, 0, (VkDeviceSize)16 * n},
        {b.contactN, 0, (VkDeviceSize)16 * n},
        {b.contactL, 0, (VkDeviceSize)4 * n},
    };
    vkMakeSet(dev, m_softPool, m_softDsl, softBinds(), b.simSet, bi);
}

void SoftSim::dispatch(VkCommandBuffer cmd, GpuBody& b, int mode, int n, int groupOffset) {
    int pc[4] = {mode, n, groupOffset, 0};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPl, 0, 1, &b.simSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_softPl, VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)sizeof(pc), pc);
    vkCmdDispatch(cmd, (n + 63) / 64, 1, 1);
}

void SoftSim::recordMode(VkCommandBuffer cmd, GpuBody& b, int mode, int n, int groupOffset) {
    if (n <= 0)
        return;
    VkBufferMemoryBarrier bmb[5] = {bufBarrier(b.vtx), bufBarrier(b.prev), bufBarrier(b.sub0), bufBarrier(b.contactN),
                                    bufBarrier(b.contactL)};
    stageBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, bmb, 5);
    dispatch(cmd, b, mode, n, groupOffset);
}

void SoftSim::recordBody(VkCommandBuffer cmd, GpuBody& b, int iters, int pinned) {
    if (pinned)
        return;
    recordMode(cmd, b, 4, b.n, 0);
    recordMode(cmd, b, 0, b.n, 0);
    for (int it = 0; it < iters; ++it)
        for (int c = 0; c < b.colorCount; ++c)
            recordMode(cmd, b, 1, b.colorStart[c + 1] - b.colorStart[c], b.colorStart[c]);
    recordMode(cmd, b, 2, b.n, 0);
    recordMode(cmd, b, 5, b.n, 0);
    recordMode(cmd, b, 3, b.n, 0);
}

void SoftSim::record(VkCommandBuffer cmd, const SimParams& p, int clothPinned) {
    PhysParams phys;
    phys.nCaps = m_nCaps;
    phys.dt = kFrameDt / sim::kSubsteps;
    phys.damping = p.damping;
    phys.gravity = sim::kGravity * p.mass;
    phys.friction = p.friction;
    phys.skin = 0.01f;
    phys.fricMargin = 0.2f;
    phys.tension = p.tension;
    phys.stiff = p.stiffness;
    phys.maxStep = SOFT_MAX_STEP;
    void* up;
    VK(vkMapMemory(m_dev, m_physParamsMem, 0, 40, 0, &up));
    std::memcpy(up, &phys, 40);
    vkUnmapMemory(m_dev, m_physParamsMem);

    std::vector<VkBufferMemoryBarrier> first;
    for (GpuBody& b : m_body) {
        first.push_back(bufBarrier(b.vtx));
        first.push_back(bufBarrier(b.prev));
        first.push_back(bufBarrier(b.sub0));
        first.push_back(bufBarrier(b.contactN));
        first.push_back(bufBarrier(b.contactL));
    }
    stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, first.data(),
                 (uint32_t)first.size());

    const int pinned[kCount] = {clothPinned, 0};
    for (int i = 0; i < kCount; ++i)
        recordBody(cmd, m_body[i], p.passes, pinned[i]);

    for (GpuBody& b : m_body) {
        VkBufferMemoryBarrier bmb = bufBarrier(b.vtx);
        stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, &bmb, 1);
    }
}

void SoftSim::resetBody(GpuBody& b) {
    void* pvtx;
    VK(vkMapMemory(m_dev, b.vtxMem, 0, (VkDeviceSize)48 * b.n, 0, &pvtx));
    std::memcpy(pvtx, b.initVtx.data(), (size_t)48 * b.n);
    vkUnmapMemory(m_dev, b.vtxMem);
    void* pv;
    VK(vkMapMemory(m_dev, b.prevMem, 0, (VkDeviceSize)16 * b.n, 0, &pv));
    std::memcpy(pv, b.initPos.data(), (size_t)16 * b.n);
    vkUnmapMemory(m_dev, b.prevMem);
    void* ps;
    VK(vkMapMemory(m_dev, b.sub0Mem, 0, (VkDeviceSize)16 * b.n, 0, &ps));
    std::memcpy(ps, b.initPos.data(), (size_t)16 * b.n);
    vkUnmapMemory(m_dev, b.sub0Mem);
    void* cn;
    VK(vkMapMemory(m_dev, b.contactNMem, 0, (VkDeviceSize)16 * b.n, 0, &cn));
    std::memset(cn, 0, (size_t)16 * b.n);
    vkUnmapMemory(m_dev, b.contactNMem);
    void* cl;
    VK(vkMapMemory(m_dev, b.contactLMem, 0, (VkDeviceSize)4 * b.n, 0, &cl));
    std::memset(cl, 0, (size_t)4 * b.n);
    vkUnmapMemory(m_dev, b.contactLMem);
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
        db(b.vtx, b.vtxMem);
        db(b.prev, b.prevMem);
        db(b.sub0, b.sub0Mem);
        db(b.contactN, b.contactNMem);
        db(b.contactL, b.contactLMem);
        db(b.entries, b.entriesMem);
        db(b.entryStart, b.entryStartMem);
        db(b.colorVerts, b.colorVertsMem);
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
