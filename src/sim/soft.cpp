#include "sim/soft.hpp"

#include "ecs.hpp"
#include "softbody_spv.hpp"
#include "vk/vkutil.hpp"
#include <cmath>
#include <cstdio>

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

}

void Soft::init(VkDevice dev, VkPhysicalDevice pdev) {
    m_dev = dev;
    m_pdev = pdev;
}

void Soft::setRequestVerts(std::function<GpuMeshRef(const entt::registry&, const Mesh&, entt::entity)> request) {
    m_request = std::move(request);
}

void Soft::setFloor(float y) {
    m_floorY = y;
}

void Soft::syncCapsules(const entt::registry& reg) {
    CapsuleGPU* caps = m_caps.data();
    int n = 0;
    const auto push = [&](entt::entity e, const std::vector<entt::entity>& cols) {
        const auto* wt = reg.try_get<WorldTransform>(e);
        if (!wt)
            return;
        for (entt::entity c : cols) {
            const auto* col = reg.try_get<Collider>(c);
            auto* cs = col ? reg.try_get<CapsuleShape>(col->shape) : nullptr;
            if (!cs || n >= kMaxCapsules)
                continue;
            Transform t = *wt;
            if (const auto* lt = reg.try_get<Transform>(c))
                t = transformCompose(t, *lt);
            fillCapsule(caps[n++], t.pos, t.quat, cs->halfLen, cs->radius);
        }
    };
    for (auto [e, rb] : reg.view<RigidBody>().each())
        push(e, rb.colliders);
    for (auto [e, kb] : reg.view<KinematicBody>().each())
        push(e, kb.colliders);
    uploadCapsules(n);
}

void Soft::project(const entt::registry& reg) {
    for (auto [e, sb] : reg.view<SoftBodyData>().each()) {
        if (m_ent.count(e))
            continue;
        const auto fail = [this, &reg, e](const char* why) {
            if (m_warned.insert(e).second) {
                const auto* n = reg.try_get<Name>(e);
                std::fprintf(stderr, "3dsim: soft body '%s': %s\n", n ? n->id.c_str() : "?", why);
            }
        };
        const auto* rend = reg.try_get<Renderable>(e);
        const auto* geom = rend ? reg.try_get<Geometry>(rend->geom) : nullptr;
        if (!geom) {
            fail("no geometry (Renderable.geom)");
            continue;
        }
        const int nv = geom->mesh.vertexCount();
        if (nv <= 0 || sb.cons.size() % 4 != 0) {
            fail("bad vertex or constraint data (cons = flat {a, b, rest, k} quads)");
            continue;
        }
        bool bad = false;
        for (size_t i = 0; i + 3 < sb.cons.size(); i += 4)
            if (sb.cons[i] < 0 || sb.cons[i] >= nv || sb.cons[i + 1] < 0 || sb.cons[i + 1] >= nv) {
                bad = true;
                break;
            }
        if (bad) {
            fail("constraint vertex index out of range");
            continue;
        }
        std::vector<sim::Constraint> cons;
        cons.reserve(sb.cons.size() / 4);
        for (size_t i = 0; i + 3 < sb.cons.size(); i += 4)
            cons.push_back({(int)sb.cons[i], (int)sb.cons[i + 1], sb.cons[i + 2], sb.cons[i + 3]});
        Mesh m = geom->mesh;
        Transform t{};
        if (reg.try_get<Transform>(e))
            t = worldTransform(reg, e);
        for (int i = 0; i < nv; ++i) {
            const V3 v = vAdd(quatRotate(t.quat, V3{m.pos[3 * i], m.pos[3 * i + 1], m.pos[3 * i + 2]}), t.pos);
            m.pos[3 * i] = v.x;
            m.pos[3 * i + 1] = v.y;
            m.pos[3 * i + 2] = v.z;
            if (m.nrm.size() == m.pos.size()) {
                const V3 n = quatRotate(t.quat, V3{m.nrm[3 * i], m.nrm[3 * i + 1], m.nrm[3 * i + 2]});
                m.nrm[3 * i] = n.x;
                m.nrm[3 * i + 1] = n.y;
                m.nrm[3 * i + 2] = n.z;
            }
        }
        if (!m_request) {
            fail("no vertex source wired");
            continue;
        }
        const GpuMeshRef ref = m_request(reg, m, rend->geom);
        addSoftBody(e, m, cons, ref.rw);
    }
}

void Soft::addSoftBody(entt::entity e, const Mesh& mesh, const std::vector<sim::Constraint>& cons, const MeshGpu& rw) {
    GpuBody b;
    b.soft.init(mesh, cons);
    const auto vn = (VkDeviceSize)b.soft.n;
    b.pos = rw.pos.buffer;
    b.posMem = rw.pos.memory;
    b.nrm = rw.nrm.buffer;
    b.nrmMem = rw.nrm.memory;
    auto mk = [this](VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize bytes, const void* data) {
        vkMakeBufferDev(m_dev, m_pdev, buf, mem, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, data);
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
    vkMakeBuffer(m_dev, m_pdev, b.paramsBuf, b.paramsMem, 40, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    m_body.push_back(std::move(b));
    m_ent[e] = (int)m_body.size() - 1;
    if (!m_softPools.empty()) {
        ensurePoolSlot();
        makeBodySet(m_body.back());
        ++m_softPoolUsed;
    }
}

void Soft::build() {
    m_caps.resize(kMaxCapsules);
    vkMakeBuffer(m_dev, m_pdev, m_capsInstances, m_capsInstancesMem, 48 * kMaxCapsules,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    const auto binds = softBinds();
    auto maxSets = (uint32_t)m_body.size();
    if (maxSets == 0)
        maxSets = 1;
    VkDescriptorPool pool;
    vkMakeDslPool(m_dev, binds, maxSets, m_softDsl, pool);
    m_softPools.push_back(pool);
    m_softPoolCap = (int)maxSets;
    m_softPoolUsed = 0;
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
    for (auto& b : m_body) {
        makeBodySet(b);
        ++m_softPoolUsed;
    }
}

void Soft::ensurePoolSlot() {
    if (m_softPoolUsed < m_softPoolCap)
        return;
    m_softPools.push_back(vkMakePool(m_dev, softBinds(), (uint32_t)kPoolChunk));
    m_softPoolCap = kPoolChunk;
    m_softPoolUsed = 0;
}

void Soft::makeBodySet(GpuBody& b) {
    const int n = b.soft.n;
    std::vector<VkDescriptorBufferInfo> bi = {
        {b.pos, 0, (VkDeviceSize)12 * n},
        {b.nrm, 0, (VkDeviceSize)12 * n},
        {b.prev, 0, (VkDeviceSize)12 * n},
        {m_capsInstances, 0, 48 * (VkDeviceSize)kMaxCapsules},
        {b.entries, 0, (VkDeviceSize)16 * b.soft.entries.size()},
        {b.entryStart, 0, (VkDeviceSize)4 * (n + 1)},
        {b.colorVerts, 0, (VkDeviceSize)4 * n},
        {b.paramsBuf, 0, 40},
        {b.sub0, 0, (VkDeviceSize)12 * n},
        {b.contactN, 0, (VkDeviceSize)16 * n},
        {b.contactL, 0, (VkDeviceSize)4 * n},
        {b.tris, 0, (VkDeviceSize)4 * b.soft.triList.size()},
        {b.triStart, 0, (VkDeviceSize)4 * (n + 1)},
        {b.triList, 0, (VkDeviceSize)4 * b.soft.triList.size()},
    };
    vkMakeSet(m_dev, m_softPools.back(), m_softDsl, softBinds(), b.simSet, bi);
}

void Soft::dispatch(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset) {
    int pc[4] = {mode, n, groupOffset, 0};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPl, 0, 1, &b.simSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_softPl, VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)sizeof(pc), pc);
    vkCmdDispatch(cmd, (n + 63) / 64, 1, 1);
}

int Soft::modeBarriers(const GpuBody& b, int mode, VkBufferMemoryBarrier* out) {
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

void Soft::recordMode(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset) {
    if (n <= 0)
        return;
    VkBufferMemoryBarrier bmb[6];
    const int nb = modeBarriers(b, mode, bmb);
    stageBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, bmb, (uint32_t)nb);
    dispatch(cmd, b, mode, n, groupOffset);
}

void Soft::recordBody(VkCommandBuffer cmd, const GpuBody& b, int iters, int pinned) {
    if (pinned)
        return;
    const int n = b.soft.n;
    recordMode(cmd, b, 0, n, 0);
    for (int it = 0; it < iters; ++it)
        for (int c = 0; c < b.soft.colorCount; ++c)
            recordMode(cmd, b, 1, b.soft.colorStart[c + 1] - b.soft.colorStart[c], b.soft.colorStart[c]);
    recordMode(cmd, b, 2, n, 0);
}

void Soft::setPinned(entt::entity e, int pinned) {
    const auto it = m_ent.find(e);
    if (it != m_ent.end())
        m_body[(size_t)it->second].pinned = pinned;
}

void Soft::setParams(entt::entity e, const SimParams& params, int steps) {
    const auto it = m_ent.find(e);
    if (it == m_ent.end())
        return;
    m_body[(size_t)it->second].params = params;
    m_body[(size_t)it->second].steps = steps;
}

void Soft::onBodyDestroyed(entt::registry&, entt::entity e) {
    const auto it = m_ent.find(e);
    if (it == m_ent.end())
        return;
    m_body[(size_t)it->second].dead = true;
    m_free.push_back(it->second);
    m_ent.erase(it);
}

void Soft::watch(entt::registry& reg) {
    reg.on_destroy<SoftBodyData>().connect<&Soft::onBodyDestroyed>(this);
}

void Soft::flushFrees() {
    for (const int id : m_free) {
        GpuBody& b = m_body[(size_t)id];
        freeBody(b);
    }
    m_free.clear();
}

void Soft::record(VkCommandBuffer cmd) {
    for (GpuBody& b : m_body) {
        if (b.dead)
            continue;
        PhysParams phys{};
        phys.nCaps = m_nCaps;
        phys.dt = kFrameDt / (float)b.steps;
        phys.damping = std::pow(b.params.damping, 1.0f / (float)b.steps);
        phys.gravity = sim::kGravity * b.params.mass;
        phys.friction = b.params.friction;
        phys.skin = kSkin;
        phys.tension = b.params.tension;
        phys.stiff = b.params.stiffness;
        phys.maxStep = kSoftMaxStep;
        phys.floorY = m_floorY;
        vkWriteBuffer(m_dev, b.paramsMem, &phys, 40);
    }

    std::vector<VkBufferMemoryBarrier> first;
    for (const GpuBody& b : m_body)
        if (!b.dead)
            for (VkBuffer buf : {b.pos, b.nrm, b.prev, b.sub0, b.contactN, b.contactL, b.tris, b.triStart, b.triList})
                first.push_back(bufBarrier(buf));
    stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, first.data(),
                 (uint32_t)first.size());

    for (const GpuBody& b : m_body)
        if (!b.dead)
            for (int s = 0; s < b.steps; ++s)
                recordBody(cmd, b, b.params.passes, b.pinned);
    for (const GpuBody& b : m_body)
        if (!b.dead && !b.pinned)
            recordMode(cmd, b, 3, b.soft.n, 0);

    for (const GpuBody& b : m_body)
        if (!b.dead) {
            VkBufferMemoryBarrier bmb[2] = {bufBarrier(b.pos), bufBarrier(b.nrm)};
            stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, bmb, 2);
        }
}

void Soft::freeBody(GpuBody& b) {
    if (b.freed)
        return;
    b.freed = true;
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

void Soft::uploadCapsules(int n) {
    m_nCaps = n;
    if (n == 0)
        return;
    vkWriteBuffer(m_dev, m_capsInstancesMem, m_caps.data(), 48 * (VkDeviceSize)n);
}

void Soft::reset() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    flushFrees();
    for (auto& b : m_body)
        freeBody(b);
    m_body.clear();
    m_ent.clear();
    m_warned.clear();
    m_free.clear();
    for (auto& p : m_softPools)
        if (p)
            vkDestroyDescriptorPool(m_dev, p, nullptr);
    m_softPools.clear();
    m_softPoolCap = 0;
    m_softPoolUsed = 0;
    if (m_softDsl)
        vkDestroyDescriptorSetLayout(m_dev, m_softDsl, nullptr);
    m_softDsl = VK_NULL_HANDLE;
    if (m_softPl)
        vkDestroyPipelineLayout(m_dev, m_softPl, nullptr);
    m_softPl = VK_NULL_HANDLE;
    if (m_softPipe)
        vkDestroyPipeline(m_dev, m_softPipe, nullptr);
    m_softPipe = VK_NULL_HANDLE;
    vkFreeBuffer(m_dev, m_capsInstances, m_capsInstancesMem);
    m_capsInstances = VK_NULL_HANDLE;
    m_capsInstancesMem = VK_NULL_HANDLE;
    m_caps.clear();
    m_nCaps = 0;
    m_floorY = -std::numeric_limits<float>::infinity();
}

void Soft::shutdown() {
    reset();
    m_dev = VK_NULL_HANDLE;
}
