#include "sim/skin.hpp"

#include "ecs.hpp"
#include "skeleton_spv.hpp"
#include "vk/vkutil.hpp"
#include <cmath>
#include <cstring>

namespace {

const float kIdentity4[4][4] = {{1, 0, 0, 0},
                                {0, 1, 0, 0},
                                {0, 0, 1, 0},
                                {0, 0, 0, 1}};

Mat4 invertAffine(const Mat4& M) {
    const float* m = M.m;
    float r[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = m[j * 4 + i];
    const float a = r[0], b = r[1], c = r[2], d = r[3], e = r[4], f = r[5], g = r[6], h = r[7], k = r[8];
    float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    if (det == 0.0f)
        det = 1.0f; // degenerate: avoid divide-by-zero
    const float idet = 1.0f / det;
    float invr[9];
    invr[0] = (e * k - f * h) * idet;
    invr[1] = (c * h - b * k) * idet;
    invr[2] = (b * f - c * e) * idet;
    invr[3] = (f * g - d * k) * idet;
    invr[4] = (a * k - c * g) * idet;
    invr[5] = (c * d - a * f) * idet;
    invr[6] = (d * h - e * g) * idet;
    invr[7] = (g * b - a * h) * idet;
    invr[8] = (a * e - b * d) * idet;
    Mat4 inv{};
    const float tx = m[12], ty = m[13], tz = m[14];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            inv.m[j * 4 + i] = invr[i * 3 + j];
    inv.m[12] = -(invr[0] * tx + invr[1] * ty + invr[2] * tz);
    inv.m[13] = -(invr[3] * tx + invr[4] * ty + invr[5] * tz);
    inv.m[14] = -(invr[6] * tx + invr[7] * ty + invr[8] * tz);
    inv.m[15] = 1.0f;
    return inv;
}

std::vector<VkDescriptorSetLayoutBinding> skelBinds() {
    std::vector<VkDescriptorSetLayoutBinding> b(9);
    for (uint32_t i = 0; i < b.size(); ++i) {
        b[i].binding = i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return b;
}

VkBufferMemoryBarrier bufBarrier(VkBuffer buf) {
    VkBufferMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = buf;
    b.offset = 0;
    b.size = VK_WHOLE_SIZE;
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    return b;
}

}

void Skin::init(VkDevice dev, VkPhysicalDevice pdev) {
    m_dev = dev;
    m_pdev = pdev;
}

void Skin::addSkeleton(entt::entity e, int nJoints) {
    Skel c;
    c.dqStaging.resize((size_t)nJoints);
    const auto bytes = (VkDeviceSize)nJoints * sizeof(DualQuat);
    vkMakeBuffer(m_dev, m_pdev, c.jointDQ.b, c.jointDQ.m, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    c.jointDQ.sz = bytes;
    m_skels.push_back(std::move(c));
    m_ent[e] = (int)m_skels.size() - 1;
}

void Skin::linkMesh(entt::entity mesh, int groupId) {
    m_meshGroup[mesh] = groupId;
}

int Skin::addGroup(entt::entity e, const SkelGroupDesc& d) {
    const auto ce = m_ent.find(e);
    if (ce == m_ent.end())
        return -1;
    const int skelGpu = ce->second;
    Group g;
    g.skelGpu = skelGpu;
    g.nV = d.nV;
    const int nT = d.nV > 0 ? (int)(d.deltas.size() / ((size_t)d.nV * 3)) : 0;
    g.morphW0 = d.initWeights;
    g.morphW0.resize((size_t)nT, 0.0f);
    g.morphW = g.morphW0;
    for (size_t i = 0; i < d.morphNames.size() && i < (size_t)nT; ++i)
        g.nameToTarget[d.morphNames[i]] = (int)i;
    const auto vn = (VkDeviceSize)d.nV;
    auto mkDev = [this](Buf& b, VkDeviceSize bytes, const void* data) {
        vkMakeBufferDev(m_dev, m_pdev, b.b, b.m, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, data);
        b.sz = bytes;
    };
    mkDev(g.pos0, 12 * vn, d.pos0.data());
    mkDev(g.nrm0, 12 * vn, d.nrm0.data());
    mkDev(g.joints, 16 * vn, d.joints.data());
    mkDev(g.weights, 16 * vn, d.weights.data());
    g.cpuPos = d.pos0;
    g.cpuJoints = d.joints;
    g.cpuWeights = d.weights;
    if (nT > 0)
        mkDev(g.deltas, 12 * vn * (VkDeviceSize)nT, d.deltas.data());
    else
        mkDev(g.deltas, 4, nullptr);
    vkMakeBuffer(m_dev, m_pdev, g.activeMorphs.b, g.activeMorphs.m, 8 * kMaxActiveMorphs,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    g.activeMorphs.sz = 8 * kMaxActiveMorphs;
    mkDev(g.posOut, 12 * vn, d.pos0.data());
    mkDev(g.nrmOut, 12 * vn, d.nrm0.data());
    m_groups.push_back(std::move(g));
    m_skels[(size_t)skelGpu].groups.push_back((int)m_groups.size() - 1);
    if (!m_pools.empty()) {
        ensurePoolSlot();
        makeGroupSet(m_groups.back());
        ++m_poolUsed;
    }
    return (int)m_groups.size() - 1;
}

MeshGpu Skin::groupOut(int groupId) const {
    const Group& g = m_groups[groupId];
    return {VertexStore{g.posOut.b, g.posOut.m, (uint32_t)g.nV}, VertexStore{g.nrmOut.b, g.nrmOut.m, (uint32_t)g.nV}};
}

void Skin::build() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    const auto binds = skelBinds();
    auto maxSets = (uint32_t)m_groups.size();
    if (maxSets == 0)
        maxSets = 1;
    VkDescriptorPool pool;
    vkMakeDslPool(m_dev, binds, maxSets, m_dsl, pool);
    m_pools.push_back(pool);
    m_poolCap = (int)maxSets;
    m_poolUsed = 0;
    m_pl = vkMakePipelineLayoutPC(m_dev, m_dsl, VK_SHADER_STAGE_COMPUTE_BIT, 16);
    VkShaderModule cm = vkMakeModule(m_dev, skeleton_spv, skeleton_spv_len / 4);
    VkPipelineShaderStageCreateInfo cs{};
    cs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cs.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cs.module = cm;
    cs.pName = "main";
    VkComputePipelineCreateInfo cpc{};
    cpc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpc.stage = cs;
    cpc.layout = m_pl;
    VK(vkCreateComputePipelines(m_dev, VK_NULL_HANDLE, 1, &cpc, nullptr, &m_pipe));
    vkDestroyShaderModule(m_dev, cm, nullptr);
    for (auto& g : m_groups) {
        makeGroupSet(g);
        ++m_poolUsed;
    }
}

void Skin::ensurePoolSlot() {
    if (m_poolUsed < m_poolCap)
        return;
    m_pools.push_back(vkMakePool(m_dev, skelBinds(), (uint32_t)kPoolChunk));
    m_poolCap = kPoolChunk;
    m_poolUsed = 0;
}

void Skin::makeGroupSet(Group& g) {
    const Skel& c = m_skels[(size_t)g.skelGpu];
    std::vector<VkDescriptorBufferInfo> bi = {
        {g.pos0.b, 0, g.pos0.sz},
        {g.nrm0.b, 0, g.nrm0.sz},
        {g.joints.b, 0, g.joints.sz},
        {g.weights.b, 0, g.weights.sz},
        {g.deltas.b, 0, g.deltas.sz},
        {c.jointDQ.b, 0, c.jointDQ.sz},
        {g.activeMorphs.b, 0, g.activeMorphs.sz},
        {g.posOut.b, 0, g.posOut.sz},
        {g.nrmOut.b, 0, g.nrmOut.sz},
    };
    vkMakeSet(m_dev, m_pools.back(), m_dsl, skelBinds(), g.set, bi);
}

void Skin::stepSkeleton(const entt::registry& reg, const Skeleton& s, Skel& c) {
    auto worldOf = [&](entt::entity e) {
        if (e != entt::null)
            if (const auto* wt = reg.try_get<WorldTransform>(e))
                return wt->toMat4();
        return mat4Identity();
    };
    const Mat4 invPlace = invertAffine(worldOf(s.placement));
    for (size_t k = 0; k < s.joints.size(); ++k) {
        const Mat4 M = mul4(mul4(invPlace, worldOf(s.joints[k])), s.invBinds[k]);
        float mm[4][4];
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                mm[col][row] = M.m[col * 4 + row];
        dq::mat4ToDquat(&c.dqStaging[k], kIdentity4, mm);
    }
    vkWriteBuffer(m_dev, c.jointDQ.m, c.dqStaging.data(), c.jointDQ.sz);
}

void Skin::packMorphs(Group& g) {
    uint32_t pack[kMaxActiveMorphs * 2] = {};
    int k = 0;
    for (size_t t = 0; t < g.morphW.size() && k < kMaxActiveMorphs; ++t) {
        const float wgt = g.morphW[t];
        if (std::fabs(wgt) < 1e-4f)
            continue;
        uint32_t bits;
        std::memcpy(&bits, &wgt, 4);
        pack[k * 2] = (uint32_t)t;
        pack[k * 2 + 1] = bits;
        ++k;
    }
    g.activeCount = k;
    vkWriteBuffer(m_dev, g.activeMorphs.m, pack, g.activeMorphs.sz);
}

void Skin::step(const entt::registry& reg, VkCommandBuffer cmd) {
    if (m_pl == VK_NULL_HANDLE)
        return;
    for (const auto e : reg.view<Skeleton>()) {
        const auto ce = m_ent.find(e);
        if (ce == m_ent.end() || m_skels[(size_t)ce->second].dead)
            continue;
        const Skeleton& s = reg.get<Skeleton>(e);
        Skel& c = m_skels[(size_t)ce->second];
        stepSkeleton(reg, s, c);
        for (int gid : c.groups)
            if ((size_t)gid < m_groups.size() && !m_groups[gid].dead)
                packMorphs(m_groups[gid]);
        for (int gid : c.groups) {
            Group& g = m_groups[gid];
            if (g.dead || g.nV <= 0)
                continue;
            const Buf* srcs[7] = {&g.pos0, &g.nrm0, &g.joints, &g.weights, &g.deltas, &c.jointDQ, &g.activeMorphs};
            VkBufferMemoryBarrier pre[7];
            for (int i = 0; i < 7; ++i)
                pre[i] = bufBarrier(srcs[i]->b);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                                 nullptr, 7, pre, 0, nullptr);
            int pc[4] = {g.nV, (int)s.joints.size(), g.activeCount, 0};
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipe);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pl, 0, 1, &g.set, 0, nullptr);
            vkCmdPushConstants(cmd, m_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)sizeof(pc), pc);
            vkCmdDispatch(cmd, (g.nV + 63) / 64, 1, 1);
            VkBufferMemoryBarrier post[2] = {bufBarrier(g.posOut.b), bufBarrier(g.nrmOut.b)};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0,
                                 nullptr, 2, post, 0, nullptr);
        }
    }
}

std::unordered_map<entt::entity, int>::const_iterator Skin::skelFor(const entt::registry& reg, entt::entity skel) const {
    if (!reg.valid(skel) || !reg.all_of<Skeleton>(skel))
        return m_ent.end();
    const auto ce = m_ent.find(skel);
    if (ce == m_ent.end() || m_skels[(size_t)ce->second].dead)
        return m_ent.end();
    return ce;
}

void Skin::setMorph(const entt::registry& reg, entt::entity skel, const std::string& name, float weight) {
    const auto ce = skelFor(reg, skel);
    if (ce == m_ent.end())
        return;
    for (int gid : m_skels[(size_t)ce->second].groups) {
        auto it = m_groups[gid].nameToTarget.find(name);
        if (it != m_groups[gid].nameToTarget.end())
            m_groups[gid].morphW[(size_t)it->second] = weight;
    }
}

int Skin::meshRefs(int groupId) const {
    int refs = 0;
    for (const auto& [m, g] : m_meshGroup)
        if (g == groupId)
            ++refs;
    return refs;
}

void Skin::maybeFreeGroup(int groupId) {
    Group& g = m_groups[(size_t)groupId];
    if (g.freed || !g.dead)
        return;
    if (!m_skels[(size_t)g.skelGpu].dead)
        return;
    g.freed = true;
    m_freeGroups.push_back(groupId);
}

void Skin::destroySkeleton(entt::entity e) {
    const auto ce = m_ent.find(e);
    if (ce == m_ent.end())
        return;
    const int skelGpu = ce->second;
    m_ent.erase(ce);
    if (m_skels[(size_t)skelGpu].dead)
        return;
    m_skels[(size_t)skelGpu].dead = true;
    for (int gid : m_skels[(size_t)skelGpu].groups) {
        m_groups[(size_t)gid].dead = true;
        maybeFreeGroup(gid);
    }
}

void Skin::onSkeletonDestroyed(entt::registry&, entt::entity e) {
    destroySkeleton(e);
}

void Skin::onMeshDestroyed(entt::registry&, entt::entity e) {
    const auto it = m_meshGroup.find(e);
    if (it == m_meshGroup.end())
        return;
    const int gid = it->second;
    m_meshGroup.erase(it);
    if (meshRefs(gid) == 0) {
        m_groups[(size_t)gid].dead = true;
        maybeFreeGroup(gid);
    }
}

void Skin::releaseGroupBufs(Group& g) {
    Buf* bufs[8] = {&g.pos0, &g.nrm0, &g.joints, &g.weights, &g.deltas, &g.activeMorphs, &g.posOut, &g.nrmOut};
    for (Buf* bp : bufs) {
        if (bp->b == VK_NULL_HANDLE)
            continue;
        vkFreeBuffer(m_dev, bp->b, bp->m);
        bp->b = VK_NULL_HANDLE;
        bp->m = VK_NULL_HANDLE;
    }
}

void Skin::flushFrees() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    for (const int gid : m_freeGroups)
        releaseGroupBufs(m_groups[(size_t)gid]);
    m_freeGroups.clear();
}

void Skin::watch(entt::registry& reg) {
    reg.on_destroy<Skeleton>().connect<&Skin::onSkeletonDestroyed>(this);
    reg.on_destroy<Renderable>().connect<&Skin::onMeshDestroyed>(this);
}

void Skin::reset() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    flushFrees();
    m_ent.clear();
    m_meshGroup.clear();
    for (auto& c : m_skels)
        vkFreeBuffer(m_dev, c.jointDQ.b, c.jointDQ.m);
    m_skels.clear();
    for (auto& g : m_groups)
        releaseGroupBufs(g);
    m_groups.clear();
    for (auto& p : m_pools)
        if (p)
            vkDestroyDescriptorPool(m_dev, p, nullptr);
    m_pools.clear();
    m_poolCap = 0;
    m_poolUsed = 0;
    if (m_dsl)
        vkDestroyDescriptorSetLayout(m_dev, m_dsl, nullptr);
    m_dsl = VK_NULL_HANDLE;
    if (m_pl)
        vkDestroyPipelineLayout(m_dev, m_pl, nullptr);
    m_pl = VK_NULL_HANDLE;
    if (m_pipe)
        vkDestroyPipeline(m_dev, m_pipe, nullptr);
    m_pipe = VK_NULL_HANDLE;
}

void Skin::shutdown() {
    reset();
    m_dev = VK_NULL_HANDLE;
}
