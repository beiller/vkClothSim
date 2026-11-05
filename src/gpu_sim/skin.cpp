#include "gpu_sim/skin.hpp"

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

float dot4(const float a[4], const float b[4]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}
void negateDq(DualQuat* dq) {
    for (int i = 0; i < 4; ++i) {
        dq->quat[i] = -dq->quat[i];
        dq->trans[i] = -dq->trans[i];
    }
}
void copy4(float d[4], const float s[4]) { std::memcpy(d, s, 16); }

}

void Skin::init(VkDevice dev, VkPhysicalDevice pdev) {
    m_dev = dev;
    m_pdev = pdev;
}

void Skin::setRequestVerts(std::function<GpuMeshRef(const entt::registry&, const Mesh&, entt::entity)> request) {
    m_request = std::move(request);
}

void Skin::sync(const entt::registry& reg) {
    if (m_dev == VK_NULL_HANDLE)
        return;
    for (const auto e : reg.view<Skeleton>()) {
        const Skeleton& s = reg.get<Skeleton>(e);
        const auto ce = m_ent.find(e);
        int skelGpu = ce != m_ent.end() ? ce->second : -1;
        if (skelGpu < 0) {
            Skel c;
            c.dqStaging.resize(s.joints.size());
            c.prevQuat.resize(4 * s.joints.size(), 0.0f);
            for (size_t k = 0; k < s.joints.size(); ++k)
                c.prevQuat[4 * k] = 1.0f;
            const auto bytes = (VkDeviceSize)s.joints.size() * sizeof(DualQuat);
            if (bytes > 0)
                vkMakeBuffer(m_dev, m_pdev, c.jointDQ.b, c.jointDQ.m, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
            c.jointDQ.sz = bytes;
            m_skels.push_back(std::move(c));
            skelGpu = (int)m_skels.size() - 1;
            m_ent[e] = skelGpu;
        }
        if (m_skels[(size_t)skelGpu].dead)
            continue;
        for (const entt::entity m : s.meshes)
            adoptState(reg, s, m, skelGpu);
    }
}

void Skin::adoptState(const entt::registry& reg, const Skeleton& s, entt::entity mesh, int skelGpu) {
    if (m_meshState.count(mesh))
        return;
    if (s.joints.empty())
        return;
    const auto* gc = reg.try_get<Geometry>(mesh);
    if (!gc)
        return;
    const Mesh& m = gc->mesh;
    const int nV = m.vertexCount();
    if (nV <= 0 || m.joints.size() != (size_t)4 * nV || m.weights.size() != (size_t)4 * nV)
        return;
    MeshState st;
    st.mesh = mesh;
    st.skelGpu = skelGpu;
    st.nV = nV;
    if (!m_request)
        return;
    st.out = m_request(reg, m, mesh).rw;
    const auto vn = (VkDeviceSize)nV;
    auto mkDev = [this](Buf& b, VkDeviceSize bytes, const void* data) {
        vkMakeBufferDev(m_dev, m_pdev, b.b, b.m, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, data);
        b.sz = bytes;
    };
    const int nT = (int)m.shapes.size();
    uint64_t hash = 0xcbf29ce484222325ULL;
    auto fnv = [&hash](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        for (size_t i = 0; i < n; ++i) {
            hash ^= b[i];
            hash *= 0x100000001b3ULL;
        }
    };
    fnv(&nV, 4);
    fnv(&nT, 4);
    for (const SubMesh& sh : m.shapes) {
        fnv(sh.name.data(), sh.name.size());
        float w = sh.weight;
        fnv(&w, 4);
        fnv(sh.pos.data(), sh.pos.size() * 4);
    }
    const auto hit = m_shapeKey.find(hash);
    const bool share = hit != m_shapeKey.end() && m_shapeSets[(size_t)hit->second].nV == nV &&
                       m_shapeSets[(size_t)hit->second].nT == nT;
    if (share)
        st.shapeSet = hit->second;
    else {
        ShapeSet ss;
        ss.key = hash;
        ss.nV = nV;
        ss.nT = nT;
        if (nT > 0) {
            std::vector<float> deltas((size_t)nT * (size_t)nV * 3, 0.0f);
            for (int t = 0; t < nT; ++t)
                if (!m.shapes[t].pos.empty())
                    std::memcpy(deltas.data() + (size_t)t * (size_t)nV * 3, m.shapes[t].pos.data(), (size_t)nV * 12);
            mkDev(ss.deltas, 12 * vn * (VkDeviceSize)nT, deltas.data());
        } else
            mkDev(ss.deltas, 4, nullptr);
        ss.refs = 1;
        m_shapeSets.push_back(std::move(ss));
        st.shapeSet = (int)m_shapeSets.size() - 1;
        m_shapeKey[hash] = st.shapeSet;
    }
    mkDev(st.pos0, 12 * vn, m.pos.data());
    mkDev(st.nrm0, 12 * vn, m.nrm.data());
    mkDev(st.joints, 16 * vn, m.joints.data());
    mkDev(st.weights, 16 * vn, m.weights.data());
    vkMakeBuffer(m_dev, m_pdev, st.activeMorphs.b, st.activeMorphs.m, 8 * kMaxActiveMorphs,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    st.activeMorphs.sz = 8 * kMaxActiveMorphs;
    if (share)
        ++m_shapeSets[(size_t)st.shapeSet].refs;
    m_states.push_back(std::move(st));
    m_meshState[mesh] = (int)m_states.size() - 1;
    m_skels[(size_t)skelGpu].states.push_back((int)m_states.size() - 1);
    if (!m_pools.empty()) {
        ensurePoolSlot();
        makeStateSet(m_states.back());
        ++m_poolUsed;
    }
}

void Skin::build(const entt::registry& reg) {
    if (m_dev == VK_NULL_HANDLE)
        return;
    sync(reg);
    const auto binds = skelBinds();
    auto maxSets = (uint32_t)m_states.size();
    if (maxSets == 0)
        maxSets = 1;
    VkDescriptorPool pool;
    vkMakeDslPool(m_dev, binds, maxSets, m_dsl, pool);
    m_pools.push_back(pool);
    m_poolCap = (int)maxSets;
    m_poolUsed = 0;
    m_pl = vkMakePipelineLayoutPC(m_dev, m_dsl, VK_SHADER_STAGE_COMPUTE_BIT, 16);
    m_pipe = makeComputePipeline(m_dev, skeleton_spv, skeleton_spv_len / 4, m_pl);
    for (auto& st : m_states) {
        if (st.set != VK_NULL_HANDLE)
            continue;
        makeStateSet(st);
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

void Skin::makeStateSet(MeshState& st) {
    const Skel& c = m_skels[(size_t)st.skelGpu];
    const Buf& deltas = m_shapeSets[(size_t)st.shapeSet].deltas;
    std::vector<VkDescriptorBufferInfo> bi = {
        {st.pos0.b, 0, st.pos0.sz},
        {st.nrm0.b, 0, st.nrm0.sz},
        {st.joints.b, 0, st.joints.sz},
        {st.weights.b, 0, st.weights.sz},
        {deltas.b, 0, deltas.sz},
        {c.jointDQ.b, 0, c.jointDQ.sz},
        {st.activeMorphs.b, 0, st.activeMorphs.sz},
        {st.out.pos.buffer, 0, 12 * st.out.pos.count},
        {st.out.nrm.buffer, 0, 12 * st.out.nrm.count},
    };
    st.pool = (int)m_pools.size() - 1;
    vkMakeSet(m_dev, m_pools.back(), m_dsl, skelBinds(), st.set, bi);
}

void Skin::stepSkeleton(const entt::registry& reg, const Skeleton& s, Skel& c) {
    auto worldOf = [&](entt::entity e) {
        if (e != entt::null)
            if (const auto* wt = reg.try_get<WorldTransform>(e))
                return wt->toMat4();
        return Mat4(1.0f);
    };
    const Mat4 invPlace = glm::inverse(worldOf(s.placement));
    for (size_t k = 0; k < s.joints.size(); ++k) {
        const Mat4 ib = k < s.invBinds.size() ? s.invBinds[k] : Mat4(1.0f);
        const Mat4 M = invPlace * worldOf(s.joints[k]) * ib;
        float mm[4][4];
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                mm[col][row] = M[col][row];
        dq::mat4ToDquat(&c.dqStaging[k], kIdentity4, mm);
        // pick the DQ's sign closest to the previous frame's (temporal continuity); without it, contributions flip when relative rotation crosses 180°
        float* prev = c.prevQuat.data() + 4 * k;
        if (dot4(c.dqStaging[k].quat, prev) < 0.0f)
            negateDq(&c.dqStaging[k]);
        copy4(prev, c.dqStaging[k].quat);
    }
    if (!c.jointDQ.sz)
        return;
    vkWriteBuffer(m_dev, c.jointDQ.m, c.dqStaging.data(), c.jointDQ.sz);
}

void Skin::packMorphs(const entt::registry& reg, MeshState& st) {
    uint32_t pack[kMaxActiveMorphs * 2] = {};
    int k = 0;
    if (const auto* gc = reg.try_get<Geometry>(st.mesh))
        for (size_t t = 0; t < gc->mesh.shapes.size() && k < kMaxActiveMorphs; ++t) {
            const float wgt = gc->mesh.shapes[t].weight;
            if (std::fabs(wgt) < 1e-4f)
                continue;
            uint32_t bits;
            std::memcpy(&bits, &wgt, 4);
            pack[k * 2] = (uint32_t)t;
            pack[k * 2 + 1] = bits;
            ++k;
        }
    st.activeCount = k;
    vkWriteBuffer(m_dev, st.activeMorphs.m, pack, st.activeMorphs.sz);
}

void Skin::step(const entt::registry& reg, VkCommandBuffer cmd) {
    if (m_pl == VK_NULL_HANDLE)
        return;
    sync(reg);
    for (const auto e : reg.view<Skeleton>()) {
        const auto ce = m_ent.find(e);
        if (ce == m_ent.end() || m_skels[(size_t)ce->second].dead)
            continue;
        const Skeleton& s = reg.get<Skeleton>(e);
        Skel& c = m_skels[(size_t)ce->second];
        stepSkeleton(reg, s, c);
        for (int sid : c.states)
            if ((size_t)sid < m_states.size() && !m_states[sid].dead)
                packMorphs(reg, m_states[sid]);
        for (int sid : c.states) {
            MeshState& st = m_states[sid];
            if (st.dead || st.nV <= 0)
                continue;
            const Buf& deltas = m_shapeSets[(size_t)st.shapeSet].deltas;
            const Buf* srcs[7] = {&st.pos0, &st.nrm0, &st.joints, &st.weights, &deltas, &c.jointDQ, &st.activeMorphs};
            VkBufferMemoryBarrier pre[7];
            for (int i = 0; i < 7; ++i)
                pre[i] = bufBarrier(srcs[i]->b);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                                 nullptr, 7, pre, 0, nullptr);
            int pc[4] = {st.nV, (int)s.joints.size(), st.activeCount, 0};
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipe);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pl, 0, 1, &st.set, 0, nullptr);
            vkCmdPushConstants(cmd, m_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)sizeof(pc), pc);
            vkCmdDispatch(cmd, (st.nV + 63) / 64, 1, 1);
            VkBufferMemoryBarrier post[2] = {bufBarrier(st.out.pos.buffer), bufBarrier(st.out.nrm.buffer)};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0,
                                 nullptr, 2, post, 0, nullptr);
        }
    }
}

void Skin::maybeFreeState(int stateId) {
    MeshState& st = m_states[(size_t)stateId];
    if (st.freed || !st.dead)
        return;
    if (!m_skels[(size_t)st.skelGpu].dead)
        return;
    if (m_meshState.count(st.mesh))
        return;
    st.freed = true;
    m_freeStates.push_back(stateId);
}

void Skin::onSkeletonDestroyed(entt::registry&, entt::entity e) {
    const auto ce = m_ent.find(e);
    if (ce == m_ent.end())
        return;
    const int skelGpu = ce->second;
    m_ent.erase(ce);
    if (m_skels[(size_t)skelGpu].dead)
        return;
    m_skels[(size_t)skelGpu].dead = true;
    for (const int sid : m_skels[(size_t)skelGpu].states) {
        m_states[(size_t)sid].dead = true;
        maybeFreeState(sid);
    }
}

void Skin::onMeshDestroyed(entt::registry&, entt::entity e) {
    const auto it = m_meshState.find(e);
    if (it == m_meshState.end())
        return;
    const int sid = it->second;
    m_meshState.erase(it);
    m_states[(size_t)sid].dead = true;
    maybeFreeState(sid);
}

void Skin::releaseStateBufs(MeshState& st) {
    // free the set before the buffers it references (the set holds refs to them)
    if (st.set != VK_NULL_HANDLE && st.pool >= 0) {
        VkDescriptorPool pool = m_pools[(size_t)st.pool];
        vkFreeDescriptorSets(m_dev, pool, 1, &st.set);
        st.set = VK_NULL_HANDLE;
        st.pool = -1;
    }
    Buf* bufs[5] = {&st.pos0, &st.nrm0, &st.joints, &st.weights, &st.activeMorphs};
    for (Buf* bp : bufs) {
        if (bp->b == VK_NULL_HANDLE)
            continue;
        vkFreeBuffer(m_dev, bp->b, bp->m);
        bp->b = VK_NULL_HANDLE;
        bp->m = VK_NULL_HANDLE;
    }
    if (st.shapeSet < 0)
        return;
    ShapeSet& ss = m_shapeSets[(size_t)st.shapeSet];
    if (--ss.refs > 0)
        return;
    if (ss.deltas.b) {
        vkFreeBuffer(m_dev, ss.deltas.b, ss.deltas.m);
        ss.deltas.b = VK_NULL_HANDLE; // reset() re-frees any surviving shape sets: null so it is freed once
        ss.deltas.m = VK_NULL_HANDLE;
    }
    m_shapeKey.erase(ss.key);
    st.shapeSet = -1;
}

void Skin::flushFrees() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    for (const int sid : m_freeStates)
        releaseStateBufs(m_states[(size_t)sid]);
    m_freeStates.clear();
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
    m_meshState.clear();
    for (auto& c : m_skels)
        if (c.jointDQ.b)
            vkFreeBuffer(m_dev, c.jointDQ.b, c.jointDQ.m);
    m_skels.clear();
    for (auto& st : m_states)
        releaseStateBufs(st);
    m_states.clear();
    m_freeStates.clear();
    for (auto& ss : m_shapeSets)
        if (ss.deltas.b)
            vkFreeBuffer(m_dev, ss.deltas.b, ss.deltas.m);
    m_shapeSets.clear();
    m_shapeKey.clear();
    for (auto& p : m_pools)
        vkFree(m_dev, &p, vkDestroyDescriptorPool);
    m_pools.clear();
    m_poolCap = 0;
    m_poolUsed = 0;
    vkFree(m_dev, &m_dsl, vkDestroyDescriptorSetLayout);
    m_dsl = VK_NULL_HANDLE;
    vkFree(m_dev, &m_pl, vkDestroyPipelineLayout);
    m_pl = VK_NULL_HANDLE;
    vkFree(m_dev, &m_pipe, vkDestroyPipeline);
    m_pipe = VK_NULL_HANDLE;
}

void Skin::shutdown() {
    reset();
    m_dev = VK_NULL_HANDLE;
}
