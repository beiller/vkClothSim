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

void SoftSim::init(VkDevice dev, VkPhysicalDevice pdev) {
    m_dev = dev;
    m_pdev = pdev;
}

int SoftSim::addCapsule(const CapsuleParams& params) {
    m_colliders.push_back(params);
    return (int)m_colliders.size() - 1;
}

int SoftSim::addSoftBody(const Mesh& mesh, const std::vector<sim::Constraint>& cons, const MeshGpu& rw) {
    GpuBody b;
    b.soft.init(mesh, cons);
    const auto vn = (VkDeviceSize)b.soft.n;
    b.pos = rw.pos.buffer;
    b.posMem = rw.pos.memory;
    b.nrm = rw.nrm.buffer;
    b.nrmMem = rw.nrm.memory;
    auto mk = [this](VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize bytes, const void* data) {
        vkMakeBuffer(m_dev, m_pdev, buf, mem, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, data);
    };
    mk(b.prev, b.prevMem, 12 * vn, b.soft.pos0.data());
    mk(b.sub0, b.sub0Mem, 12 * vn, b.soft.pos0.data());
    mk(b.contactN, b.contactNMem, 16 * vn, nullptr);
    mk(b.contactL, b.contactLMem, 4 * vn, nullptr);
    mk(b.entries, b.entriesMem, 16 * b.soft.entries.size(), b.soft.entries.data());
    mk(b.entryStart, b.entryStartMem, 4 * (vn + 1), b.soft.entryStart.data());
    mk(b.colorVerts, b.colorVertsMem, 4 * vn, b.soft.colorVerts.data());
    mk(b.tris, b.trisMem, 4 * mesh.indices.size(), mesh.indices.data());
    mk(b.triStart, b.triStartMem, 4 * (vn + 1), b.soft.triStart.data());
    mk(b.triList, b.triListMem, 4 * b.soft.triList.size(), b.soft.triList.data());
    mk(b.paramsBuf, b.paramsMem, 36, nullptr);
    m_body.push_back(std::move(b));
    return (int)m_body.size() - 1;
}

void SoftSim::build() {
    const VkDeviceSize nCaps = std::max<uint32_t>(1, (uint32_t)m_colliders.size());
    vkMakeBuffer(m_dev, m_pdev, m_capsInstances, m_capsInstancesMem, 48 * nCaps, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 nullptr);
    const auto binds = softBinds();
    auto maxSets = (uint32_t)m_body.size();
    if (maxSets == 0)
        maxSets = 1;
    vkMakeDslPool(m_dev, binds, maxSets, m_softDsl, m_softPool);
    m_softPl = vkMakePipelineLayoutPC(m_dev, m_softDsl, VK_SHADER_STAGE_COMPUTE_BIT, 16);
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
        {m_capsInstances, 0, 48 * (VkDeviceSize)m_colliders.size()},
        {b.entries, 0, (VkDeviceSize)16 * b.soft.entries.size()},
        {b.entryStart, 0, (VkDeviceSize)4 * (n + 1)},
        {b.colorVerts, 0, (VkDeviceSize)4 * n},
        {b.paramsBuf, 0, 36},
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

int SoftSim::modeBarriers(const GpuBody& b, int mode, VkBufferMemoryBarrier* out) {
    int k = 0;
    auto add = [&](VkBuffer buf) {
        out[k] = bufBarrier(buf);
        ++k;
    };
    if (mode == 0) {
        add(b.pos);
        add(b.prev);
        add(b.sub0);
        add(b.contactN);
        add(b.contactL);
    } else if (mode == 1) {
        add(b.pos);
    } else if (mode == 2) {
        add(b.pos);
        add(b.sub0);
        add(b.contactN);
        add(b.contactL);
        add(b.prev);
    } else if (mode == 3) {
        add(b.pos);
        add(b.nrm);
    }
    return k;
}

void SoftSim::recordMode(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset) {
    if (n <= 0)
        return;
    VkBufferMemoryBarrier bmb[6];
    const int nb = modeBarriers(b, mode, bmb);
    stageBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, bmb, (uint32_t)nb);
    dispatch(cmd, b, mode, n, groupOffset);
}

void SoftSim::recordBody(VkCommandBuffer cmd, const GpuBody& b, int iters, int pinned) {
    if (pinned)
        return;
    const int n = b.soft.n;
    recordMode(cmd, b, 0, n, 0);
    for (int it = 0; it < iters; ++it)
        for (int c = 0; c < b.soft.colorCount; ++c)
            recordMode(cmd, b, 1, b.soft.colorStart[c + 1] - b.soft.colorStart[c], b.soft.colorStart[c]);
    recordMode(cmd, b, 2, n, 0);
}

void SoftSim::setPinned(int softId, int pinned) {
    m_body[softId].pinned = pinned;
}

void SoftSim::setParams(int softId, const SimParams& params, int steps) {
    m_body[softId].params = params;
    m_body[softId].steps = steps;
}

void SoftSim::record(VkCommandBuffer cmd) {
    for (GpuBody& b : m_body) {
        PhysParams phys{};
        phys.nCaps = (int)m_colliders.size();
        phys.dt = kFrameDt / (float)b.steps;
        phys.damping = std::pow(b.params.damping, 1.0f / (float)b.steps);
        phys.gravity = sim::kGravity * b.params.mass;
        phys.friction = b.params.friction;
        phys.skin = kSkin;
        phys.tension = b.params.tension;
        phys.stiff = b.params.stiffness;
        phys.maxStep = kSoftMaxStep;
        vkWriteBuffer(m_dev, b.paramsMem, &phys, 36);
    }

    std::vector<VkBufferMemoryBarrier> first;
    for (const GpuBody& b : m_body)
        for (VkBuffer buf : {b.pos, b.nrm, b.prev, b.sub0, b.contactN, b.contactL, b.tris, b.triStart, b.triList})
            first.push_back(bufBarrier(buf));
    stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, first.data(),
                 (uint32_t)first.size());

    for (const GpuBody& b : m_body)
        for (int s = 0; s < b.steps; ++s)
            recordBody(cmd, b, b.params.passes, b.pinned);
    for (const GpuBody& b : m_body)
        if (!b.pinned)
            recordMode(cmd, b, 3, b.soft.n, 0);

    for (const GpuBody& b : m_body) {
        VkBufferMemoryBarrier bmb[2] = {bufBarrier(b.pos), bufBarrier(b.nrm)};
        stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, bmb, 2);
    }
}

void SoftSim::resetBody(GpuBody& b) {
    const VkDeviceSize posBytes = (VkDeviceSize)12 * b.soft.n;
    for (VkDeviceMemory mem : {b.posMem, b.prevMem, b.sub0Mem})
        vkWriteBuffer(m_dev, mem, b.soft.pos0.data(), posBytes);
    vkWriteBuffer(m_dev, b.nrmMem, b.soft.nrm0.data(), posBytes);
    vkZeroBuffer(m_dev, b.contactNMem, (VkDeviceSize)16 * b.soft.n);
    vkZeroBuffer(m_dev, b.contactLMem, (VkDeviceSize)4 * b.soft.n);
}

void SoftSim::resetSoft(int softId) {
    resetBody(m_body[softId]);
}

void SoftSim::setCapsulePose(int slot, const Transform& p) {
    const CapsuleParams& c = m_colliders[slot];
    CapsuleGPU o{};
    o.centerRadius[0] = p.pos.x;
    o.centerRadius[1] = p.pos.y;
    o.centerRadius[2] = p.pos.z;
    o.centerRadius[3] = c.radius;
    o.quat[0] = p.quat.x;
    o.quat[1] = p.quat.y;
    o.quat[2] = p.quat.z;
    o.quat[3] = p.quat.w;
    o.halfLen[0] = c.halfLen;
    const VkDeviceSize off = 48 * (VkDeviceSize)slot;
    void* mem;
    VK(vkMapMemory(m_dev, m_capsInstancesMem, off, 48, 0, &mem));
    std::memcpy(mem, &o, sizeof(o));
    vkUnmapMemory(m_dev, m_capsInstancesMem);
}

void SoftSim::shutdown() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    for (auto& b : m_body) {
        vkFreeBuffer(m_dev, b.paramsBuf, b.paramsMem);
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
    vkFreeBuffer(m_dev, m_capsInstances, m_capsInstancesMem);
}
