#include "sim/softsim.hpp"

#include "softbody_spv.hpp"
#include "vk/vkutil.hpp"
#include <cmath>

namespace {

std::vector<VkDescriptorSetLayoutBinding> softBinds() {
    std::vector<VkDescriptorSetLayoutBinding> binds(14);
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

void SoftSim::registerBody(const VertexStore& pos, const VertexStore& nrm, const Mesh& mesh,
                           const std::vector<sim::Constraint>& cons) {
    GpuBody b;
    b.soft.init(mesh, cons);
    const int n = b.soft.n;
    b.pos = pos.buffer;
    b.posMem = pos.memory;
    b.nrm = nrm.buffer;
    b.nrmMem = nrm.memory;
    vkMakeBuffer(m_dev, m_pdev, b.prev, b.prevMem, (VkDeviceSize)12 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 b.soft.pos0.data());
    vkMakeBuffer(m_dev, m_pdev, b.sub0, b.sub0Mem, (VkDeviceSize)12 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 b.soft.pos0.data());
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
    vkMakeBuffer(m_dev, m_pdev, b.tris, b.trisMem, (VkDeviceSize)4 * mesh.indices.size(),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.indices.data());
    vkMakeBuffer(m_dev, m_pdev, b.triStart, b.triStartMem, (VkDeviceSize)4 * (n + 1),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, b.soft.triStart.data());
    vkMakeBuffer(m_dev, m_pdev, b.triList, b.triListMem, (VkDeviceSize)4 * b.soft.triList.size(),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, b.soft.triList.data());
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
        {b.pos, 0, (VkDeviceSize)12 * n},
        {b.nrm, 0, (VkDeviceSize)12 * n},
        {b.prev, 0, (VkDeviceSize)12 * n},
        {m_capsInstances, 0, (VkDeviceSize)48 * m_nCaps},
        {b.entries, 0, (VkDeviceSize)16 * b.soft.entries.size()},
        {b.entryStart, 0, (VkDeviceSize)4 * (n + 1)},
        {b.colorVerts, 0, (VkDeviceSize)4 * n},
        {m_physParams, 0, 36},
        {b.sub0, 0, (VkDeviceSize)12 * n},
        {b.contactN, 0, (VkDeviceSize)16 * n},
        {b.contactL, 0, (VkDeviceSize)4 * n},
        {b.tris, 0, (VkDeviceSize)4 * b.soft.triList.size()},
        {b.triStart, 0, (VkDeviceSize)4 * (n + 1)},
        {b.triList, 0, (VkDeviceSize)4 * b.soft.triList.size()},
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

std::array<VkBufferMemoryBarrier, 6> SoftSim::runtimeBarriers(const GpuBody& b) {
    return {bufBarrier(b.pos),  bufBarrier(b.nrm),      bufBarrier(b.prev),
            bufBarrier(b.sub0), bufBarrier(b.contactN), bufBarrier(b.contactL)};
}

void SoftSim::recordMode(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset) {
    if (n <= 0)
        return;
    const auto bmb = runtimeBarriers(b);
    stageBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, bmb.data(), 6);
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
    phys.damping = std::pow(p.damping, 1.0f / (float)sim::kSubsteps);
    phys.gravity = sim::kGravity * p.mass;
    phys.friction = p.friction;
    phys.skin = kSkin;
    phys.tension = p.tension;
    phys.stiff = p.stiffness;
    phys.maxStep = kSoftMaxStep;
    vkWriteBuffer(m_dev, m_physParamsMem, &phys, 36);

    std::vector<VkBufferMemoryBarrier> first;
    for (const GpuBody& b : m_body) {
        const auto rb = runtimeBarriers(b);
        first.insert(first.end(), rb.begin(), rb.end());
        first.push_back(bufBarrier(b.tris));
        first.push_back(bufBarrier(b.triStart));
        first.push_back(bufBarrier(b.triList));
    }
    stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, first.data(),
                 (uint32_t)first.size());

    for (size_t i = 0; i < m_body.size(); ++i) {
        const int pinned = (pinnedMask >> (int)i) & 1;
        for (int s = 0; s < sim::kSubsteps; ++s)
            recordBody(cmd, m_body[i], p.passes, pinned);
    }

    for (const GpuBody& b : m_body) {
        VkBufferMemoryBarrier bmb[2] = {bufBarrier(b.pos), bufBarrier(b.nrm)};
        stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, bmb, 2);
    }
}

void SoftSim::resetBody(GpuBody& b) {
    const VkDeviceSize posBytes = (VkDeviceSize)12 * b.soft.n;
    vkWriteBuffer(m_dev, b.posMem, b.soft.pos0.data(), posBytes);
    vkWriteBuffer(m_dev, b.nrmMem, b.soft.nrm0.data(), posBytes);
    vkWriteBuffer(m_dev, b.prevMem, b.soft.pos0.data(), posBytes);
    vkWriteBuffer(m_dev, b.sub0Mem, b.soft.pos0.data(), posBytes);
    vkZeroBuffer(m_dev, b.contactNMem, (VkDeviceSize)16 * b.soft.n);
    vkZeroBuffer(m_dev, b.contactLMem, (VkDeviceSize)4 * b.soft.n);
}

void SoftSim::reset(int mask) {
    for (size_t i = 0; i < m_body.size(); ++i)
        if ((mask >> (int)i) & 1)
            resetBody(m_body[i]);
}

void SoftSim::uploadCapsules(std::span<const CapsuleGPU> caps) {
    vkWriteBuffer(m_dev, m_capsInstancesMem, caps.data(), 48 * (VkDeviceSize)caps.size());
}

void SoftSim::shutdown() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    for (auto& b : m_body) {
        vkFreeBuffer(m_dev, b.prev, b.prevMem);
        vkFreeBuffer(m_dev, b.sub0, b.sub0Mem);
        vkFreeBuffer(m_dev, b.contactN, b.contactNMem);
        vkFreeBuffer(m_dev, b.contactL, b.contactLMem);
        vkFreeBuffer(m_dev, b.entries, b.entriesMem);
        vkFreeBuffer(m_dev, b.entryStart, b.entryStartMem);
        vkFreeBuffer(m_dev, b.colorVerts, b.colorVertsMem);
        vkFreeBuffer(m_dev, b.tris, b.trisMem);
        vkFreeBuffer(m_dev, b.triStart, b.triStartMem);
        vkFreeBuffer(m_dev, b.triList, b.triListMem);
    }
    if (m_softPool)
        vkDestroyDescriptorPool(m_dev, m_softPool, nullptr);
    if (m_softDsl)
        vkDestroyDescriptorSetLayout(m_dev, m_softDsl, nullptr);
    if (m_softPl)
        vkDestroyPipelineLayout(m_dev, m_softPl, nullptr);
    if (m_softPipe)
        vkDestroyPipeline(m_dev, m_softPipe, nullptr);
    vkFreeBuffer(m_dev, m_physParams, m_physParamsMem);
    vkFreeBuffer(m_dev, m_capsInstances, m_capsInstancesMem);
}
