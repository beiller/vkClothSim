#include "sim/softsim.hpp"

#include "softbody_spv.hpp"
#include "vk/vkutil.hpp"
#include <cstring>

namespace {

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

void SoftSim::init(VkDevice dev, VkPhysicalDevice pdev, int nCaps) {
    m_dev = dev;
    m_pdev = pdev;
    m_nCaps = nCaps;
}

void SoftSim::registerBody(const Mesh& mesh, const std::vector<sim::Constraint>& cons) {
    GpuBody b;
    b.soft.init(mesh, cons);
    const int n = b.soft.n;
    vkMakeBuffer(m_dev, m_pdev, b.vtx, b.vtxMem, (VkDeviceSize)48 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 b.soft.vtx.data());
    vkMakeBuffer(m_dev, m_pdev, b.prev, b.prevMem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 b.soft.pos4.data());
    vkMakeBuffer(m_dev, m_pdev, b.sub0, b.sub0Mem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 b.soft.pos4.data());
    vkMakeBuffer(m_dev, m_pdev, b.contactN, b.contactNMem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 nullptr);
    vkMakeBuffer(m_dev, m_pdev, b.contactL, b.contactLMem, (VkDeviceSize)4 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 nullptr);
    vkMakeBuffer(m_dev, m_pdev, b.entries, b.entriesMem, (VkDeviceSize)16 * b.soft.entries.size(),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, b.soft.entries.data());
    vkMakeBuffer(m_dev, m_pdev, b.entryStart, b.entryStartMem, (VkDeviceSize)4 * (n + 1),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, b.soft.entryStart.data());
    vkMakeBuffer(m_dev, m_pdev, b.colorVerts, b.colorVertsMem, (VkDeviceSize)4 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 b.soft.colorVerts.data());
    SoftDraw d;
    d.vertices = b.vtx;
    d.vertexBytes = (VkDeviceSize)48 * n;
    d.indices = mesh.indices;
    m_draw.push_back(std::move(d));
    m_body.push_back(std::move(b));
}

void SoftSim::build() {
    vkMakeBuffer(m_dev, m_pdev, m_physParams, m_physParamsMem, 36, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    vkMakeBuffer(m_dev, m_pdev, m_capsInstances, m_capsInstancesMem, 48 * (VkDeviceSize)m_nCaps,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    const auto binds = softBinds();
    auto maxSets = (uint32_t)m_body.size();
    if (maxSets == 0)
        maxSets = 1;
    vkMakeDslPool(m_dev, binds, maxSets, m_softDsl, m_softPool);
    m_softPl = vkMakePipelineLayout(m_dev, m_softDsl);
    VkShaderModule cm = vkMakeModule(m_dev, softbody_spv, softbody_spv_len / 4);
    VkPipelineShaderStageCreateInfo cs{};
    cs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cs.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cs.module = cm;
    cs.pName = "main";
    VkComputePipelineCreateInfo cpc{};
    cpc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpc.stage = cs;
    cpc.layout = m_softPl;
    VK(vkCreateComputePipelines(m_dev, VK_NULL_HANDLE, 1, &cpc, nullptr, &m_softPipe));
    vkDestroyShaderModule(m_dev, cm, nullptr);
    for (auto& b : m_body)
        makeBodySet(b);
}

void SoftSim::makeBodySet(GpuBody& b) {
    const int n = b.soft.n;
    std::vector<VkDescriptorBufferInfo> bi = {
        {b.vtx, 0, (VkDeviceSize)48 * n},
        {b.prev, 0, (VkDeviceSize)16 * n},
        {m_capsInstances, 0, (VkDeviceSize)48 * m_nCaps},
        {b.entries, 0, (VkDeviceSize)16 * b.soft.entries.size()},
        {b.entryStart, 0, (VkDeviceSize)4 * (n + 1)},
        {b.colorVerts, 0, (VkDeviceSize)4 * n},
        {m_physParams, 0, 36},
        {b.sub0, 0, (VkDeviceSize)16 * n},
        {b.contactN, 0, (VkDeviceSize)16 * n},
        {b.contactL, 0, (VkDeviceSize)4 * n},
    };
    vkMakeSet(m_dev, m_softPool, m_softDsl, softBinds(), b.simSet, bi);
}

void SoftSim::dispatch(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset) {
    int pc[4] = {mode, n, groupOffset, 0};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPl, 0, 1, &b.simSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_softPl, VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)sizeof(pc), pc);
    vkCmdDispatch(cmd, (n + 63) / 64, 1, 1);
}

void SoftSim::recordMode(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset) {
    if (n <= 0)
        return;
    VkBufferMemoryBarrier bmb[5] = {bufBarrier(b.vtx), bufBarrier(b.prev), bufBarrier(b.sub0), bufBarrier(b.contactN),
                                    bufBarrier(b.contactL)};
    stageBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, bmb, 5);
    dispatch(cmd, b, mode, n, groupOffset);
}

void SoftSim::recordBody(VkCommandBuffer cmd, const GpuBody& b, int iters, int pinned) {
    if (pinned)
        return;
    const int n = b.soft.n;
    recordMode(cmd, b, 4, n, 0);
    recordMode(cmd, b, 0, n, 0);
    for (int it = 0; it < iters; ++it)
        for (int c = 0; c < b.soft.colorCount; ++c)
            recordMode(cmd, b, 1, b.soft.colorStart[c + 1] - b.soft.colorStart[c], b.soft.colorStart[c]);
    recordMode(cmd, b, 2, n, 0);
    recordMode(cmd, b, 5, n, 0);
    recordMode(cmd, b, 3, n, 0);
}

void SoftSim::record(VkCommandBuffer cmd, const SimParams& p, int pinnedMask) {
    PhysParams phys{};
    phys.nCaps = m_nCaps;
    phys.dt = kFrameDt / sim::kSubsteps;
    phys.damping = p.damping;
    phys.gravity = sim::kGravity * p.mass;
    phys.friction = p.friction;
    phys.skin = kSkin;
    phys.tension = p.tension;
    phys.stiff = p.stiffness;
    phys.maxStep = kSoftMaxStep;
    void* up;
    VK(vkMapMemory(m_dev, m_physParamsMem, 0, 36, 0, &up));
    std::memcpy(up, &phys, 36);
    vkUnmapMemory(m_dev, m_physParamsMem);

    std::vector<VkBufferMemoryBarrier> first;
    for (const GpuBody& b : m_body) {
        first.push_back(bufBarrier(b.vtx));
        first.push_back(bufBarrier(b.prev));
        first.push_back(bufBarrier(b.sub0));
        first.push_back(bufBarrier(b.contactN));
        first.push_back(bufBarrier(b.contactL));
    }
    stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, first.data(),
                 (uint32_t)first.size());

    for (size_t i = 0; i < m_body.size(); ++i)
        recordBody(cmd, m_body[i], p.passes, (pinnedMask >> (int)i) & 1);

    for (const GpuBody& b : m_body) {
        VkBufferMemoryBarrier bmb = bufBarrier(b.vtx);
        stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, &bmb, 1);
    }
}

void SoftSim::resetBody(GpuBody& b) {
    const int n = b.soft.n;
    void* pvtx;
    VK(vkMapMemory(m_dev, b.vtxMem, 0, (VkDeviceSize)48 * n, 0, &pvtx));
    std::memcpy(pvtx, b.soft.vtx.data(), (size_t)48 * n);
    vkUnmapMemory(m_dev, b.vtxMem);
    void* pv;
    VK(vkMapMemory(m_dev, b.prevMem, 0, (VkDeviceSize)16 * n, 0, &pv));
    std::memcpy(pv, b.soft.pos4.data(), (size_t)16 * n);
    vkUnmapMemory(m_dev, b.prevMem);
    void* ps;
    VK(vkMapMemory(m_dev, b.sub0Mem, 0, (VkDeviceSize)16 * n, 0, &ps));
    std::memcpy(ps, b.soft.pos4.data(), (size_t)16 * n);
    vkUnmapMemory(m_dev, b.sub0Mem);
    void* cn;
    VK(vkMapMemory(m_dev, b.contactNMem, 0, (VkDeviceSize)16 * n, 0, &cn));
    std::memset(cn, 0, (size_t)16 * n);
    vkUnmapMemory(m_dev, b.contactNMem);
    void* cl;
    VK(vkMapMemory(m_dev, b.contactLMem, 0, (size_t)4 * n, 0, &cl));
    std::memset(cl, 0, (size_t)4 * n);
    vkUnmapMemory(m_dev, b.contactLMem);
}

void SoftSim::reset(int mask) {
    for (size_t i = 0; i < m_body.size(); ++i)
        if ((mask >> (int)i) & 1)
            resetBody(m_body[i]);
}

void SoftSim::uploadCapsules(std::span<const CapsuleGPU> caps) {
    void* p;
    VK(vkMapMemory(m_dev, m_capsInstancesMem, 0, 48 * (VkDeviceSize)caps.size(), 0, &p));
    std::memcpy(p, caps.data(), 48 * (size_t)caps.size());
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
