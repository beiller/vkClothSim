#include "gpu_sim/soft.hpp"

#include "vk/vkapp.hpp"

#include "ecs.hpp"
#include "pinupdate_spv.hpp"
#include "softbody_spv.hpp"
#include "vk/vkutil.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

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

void Soft::setVertsFor(std::function<GpuMeshRef(entt::entity)> lookup) {
    m_vertsFor = std::move(lookup);
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
            if (!col || n >= kMaxCapsules)
                continue;
            Transform t = *wt;
            if (const auto* lt = reg.try_get<Transform>(c))
                t = transformCompose(t, *lt);
            if (const auto* cs = reg.try_get<CapsuleShape>(col->shape)) {
                fillCapsule(caps[n++], t.pos, t.quat, cs->halfLen, cs->radius, cs->radius);
            } else if (const auto* tc = reg.try_get<TaperedCapsule>(col->shape)) {
                // Jolt convention: bottomRadius at the -Y end (A), topRadius at +Y (B)
                fillCapsule(caps[n++], t.pos, t.quat, tc->halfHeight, tc->bottomRadius, tc->topRadius);
            } else if (const auto* ss = reg.try_get<SphereShape>(col->shape)) {
                fillCapsule(caps[n++], t.pos, t.quat, 0.0f, ss->radius, ss->radius);
            }
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
        if (m_ent.contains(e))
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
            if (sb.cons[i] < 0.0f || sb.cons[i] >= (float)nv || sb.cons[i + 1] < 0.0f || sb.cons[i + 1] >= (float)nv) {
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
        std::vector<float> restLocal = m.pos; // local-space rest pose: the default pin anchor (restPose x WorldTransform)
        Transform t{};
        if (reg.try_get<Transform>(e))
            t = worldTransform(reg, e);
        for (int i = 0; i < nv; ++i) {
            const V3 v = t.quat * V3{m.pos[(size_t)3 * i], m.pos[(size_t)3 * i + 1], m.pos[(size_t)3 * i + 2]} + t.pos;
            m.pos[(size_t)3 * i] = v.x;
            m.pos[(size_t)3 * i + 1] = v.y;
            m.pos[(size_t)3 * i + 2] = v.z;
            if (m.nrm.size() == m.pos.size()) {
                const V3 n = t.quat * V3{m.nrm[(size_t)3 * i], m.nrm[(size_t)3 * i + 1], m.nrm[(size_t)3 * i + 2]};
                m.nrm[(size_t)3 * i] = n.x;
                m.nrm[(size_t)3 * i + 1] = n.y;
                m.nrm[(size_t)3 * i + 2] = n.z;
            }
        }
        if (!m_request) {
            fail("no vertex source wired");
            continue;
        }
        const GpuMeshRef ref = m_request(reg, m, rend->geom);
        addSoftBody(e, m, cons, ref.rw, sb.pinW, sb.pinMap, restLocal);
        if (m_posPrev)
            m_posPrev(ref.slot, m_body[(size_t)m_ent[e]].posPrev);
    }
}

void Soft::setPosPrev(std::function<void(int, VkBuffer)> fn) { m_posPrev = std::move(fn); }

void Soft::addSoftBody(entt::entity e, const Mesh& mesh, const std::vector<sim::Constraint>& cons, const MeshGpu& rw,
                       const std::vector<float>& pinW, const std::vector<uint32_t>& pinMap,
                       const std::vector<float>& restLocal) {
    GpuBody b;
    b.ent = e;
    b.soft.init(mesh, cons);
    const auto vn = (VkDeviceSize)b.soft.n;
    b.pos = rw.pos.buffer;
    b.posMem = rw.pos.memory;
    b.nrm = rw.nrm.buffer;
    b.nrmMem = rw.nrm.memory;
    // The shared pos/nrm buffer is DEVICE_LOCAL (not mappable) and may still hold the
    // renderer's local-space upload (or be mid-read). Fill it with the sim's world-space
    // initial positions via a staging copy so the first visible frame is correct regardless
    // of draw order; a pinned body never rewrites it, so wait idle first to avoid tearing.
    vkDeviceWaitIdle(m_dev);
    auto fillShared = [this](VkBuffer dst, const void* data, VkDeviceSize bytes) {
        VkBuffer stage;
        VkDeviceMemory stageMem;
        VkBufferCreateInfo sbi{};
        sbi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sbi.size = bytes;
        sbi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VK(vkCreateBuffer(m_dev, &sbi, nullptr, &stage));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(m_dev, stage, &mr);
        VkMemoryAllocateInfo maa{};
        maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        maa.allocationSize = mr.size;
        maa.memoryTypeIndex = vkFindMemoryType(m_pdev, mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK(vkAllocateMemory(m_dev, &maa, nullptr, &stageMem));
        VK(vkBindBufferMemory(m_dev, stage, stageMem, 0));
        void* p;
        VK(vkMapMemory(m_dev, stageMem, 0, bytes, 0, &p));
        std::memcpy(p, data, bytes);
        vkUnmapMemory(m_dev, stageMem);
        const VkBufferCopy r{0, 0, bytes};
        vkSubmitAndWait(m_dev, m_pdev, [&](VkCommandBuffer cb) { vkCmdCopyBuffer(cb, stage, dst, 1, &r); });
        vkDestroyBuffer(m_dev, stage, nullptr);
        vkFreeMemory(m_dev, stageMem, nullptr);
    };
    fillShared(b.pos, mesh.pos.data(), 12 * vn);
    if (!mesh.nrm.empty())
        fillShared(b.nrm, mesh.nrm.data(), 12 * vn);
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
    // rest pose in local space (uploaded once): pin targets = restPose x the node's live WorldTransform, every frame
    mk(b.restPos, b.restPosMem, 12 * vn, restLocal.data());
    // render interpolation: starts equal to pos (the sim's initial world positions); copied from pos at each tick start
    mk(b.posPrev, b.posPrevMem, 12 * vn, mesh.pos.data());
    // pin state: dense per-vertex weight/target/remap. HOST-VISIBLE (vkMakeBuffer, not the device-local mk):
    // the CPU writes weights and map at runtime, the gather dispatch (recordPinUpdates) re-derives targets every frame.
    std::vector<float> w0(vn, 0.0f);
    for (size_t i = 0; i < vn && i < pinW.size(); ++i)
        w0[i] = std::clamp(pinW[i], 0.0f, 1.0f);
    std::vector<uint32_t> map0(vn);
    for (size_t i = 0; i < vn; ++i)
        map0[i] = (i < pinMap.size()) ? pinMap[i] : (uint32_t)i;
    vkMakeBuffer(m_dev, m_pdev, b.pinW, b.pinWMem, 4 * vn, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, w0.data());
    vkMakeBuffer(m_dev, m_pdev, b.pinT, b.pinTMem, 12 * vn, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    vkMakeBuffer(m_dev, m_pdev, b.pinMap, b.pinMapMem, 4 * vn, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, map0.data());
    for (const float x : w0)
        if (x > 0.0f) {
            b.hasPins = true;
            break;
        }
    for (int sl = 0; sl < kRing; ++sl)
        vkMakeBuffer(m_dev, m_pdev, b.paramsBuf[sl], b.paramsMem[sl], 44, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    m_body.push_back(std::move(b));
    m_ent[e] = (int)m_body.size() - 1;
    if (!m_softPools.empty())
        makeBodySet(m_body.back()); // allocates one set per ring slot (grows pools as needed)
}

void Soft::build() {
    m_caps.resize(kMaxCapsules);
    for (int sl = 0; sl < kRing; ++sl)
        vkMakeBuffer(m_dev, m_pdev, m_capsInstances[sl], m_capsInstancesMem[sl], (VkDeviceSize)48 * kMaxCapsules,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    const auto binds = storageBinds(16);
    auto maxSets = (uint32_t)m_body.size() * kRing;
    if (maxSets == 0)
        maxSets = 1;
    VkDescriptorPool pool;
    vkMakeDslPool(m_dev, binds, maxSets, m_softDsl, pool);
    m_softPools.push_back(pool);
    m_softPoolCap = (int)maxSets;
    m_softPoolUsed = 0;
    m_softPl = vkMakePipelineLayoutPC(m_dev, m_softDsl, VK_SHADER_STAGE_COMPUTE_BIT, 16);
    m_softPipe = makeComputePipeline(m_dev, softbody_spv, softbody_spv_len / 4, m_softPl);
    // pin-target gather: 3 SSBOs (source pos, remap, targets) + {mat4 model; int nV; int srcN} push constants
    const auto pbinds = storageBinds(3);
    VkDescriptorSetLayoutCreateInfo dslc{};
    dslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslc.bindingCount = (uint32_t)pbinds.size();
    dslc.pBindings = pbinds.data();
    VK(vkCreateDescriptorSetLayout(m_dev, &dslc, nullptr, &m_pinDsl));
    m_pinPl = vkMakePipelineLayoutPC(m_dev, m_pinDsl, VK_SHADER_STAGE_COMPUTE_BIT, 72);
    m_pinPipe = makeComputePipeline(m_dev, pinupdate_spv, pinupdate_spv_len / 4, m_pinPl);
    for (auto& b : m_body)
        makeBodySet(b);
}

void Soft::makeBodySet(GpuBody& b) {
    const int n = b.soft.n;
    for (int sl = 0; sl < kRing; ++sl) {
        ensurePoolSlot(m_dev, storageBinds(16), kPoolChunk, m_softPools, m_softPoolCap, m_softPoolUsed);
        // order must match storageBinds(16); slot-specific entries: capsule list + phys params
        const std::vector<VkDescriptorBufferInfo> bi = {
            {b.pos, 0, (VkDeviceSize)12 * n},
            {b.nrm, 0, (VkDeviceSize)12 * n},
            {b.prev, 0, (VkDeviceSize)12 * n},
            {m_capsInstances[sl], 0, 48 * (VkDeviceSize)kMaxCapsules},
            {b.entries, 0, (VkDeviceSize)16 * b.soft.entries.size()},
            {b.entryStart, 0, (VkDeviceSize)4 * (n + 1)},
            {b.colorVerts, 0, (VkDeviceSize)4 * n},
            {b.paramsBuf[sl], 0, 44},
            {b.sub0, 0, (VkDeviceSize)12 * n},
            {b.contactN, 0, (VkDeviceSize)16 * n},
            {b.contactL, 0, (VkDeviceSize)4 * n},
            {b.tris, 0, (VkDeviceSize)4 * b.soft.triList.size()},
            {b.triStart, 0, (VkDeviceSize)4 * (n + 1)},
            {b.triList, 0, (VkDeviceSize)4 * b.soft.triList.size()},
            {b.pinW, 0, (VkDeviceSize)4 * n},
            {b.pinT, 0, (VkDeviceSize)12 * n},
        };
        b.simPool[sl] = (int)m_softPools.size() - 1;
        vkMakeSet(m_dev, m_softPools.back(), m_softDsl, storageBinds(16), b.simSet[sl], bi);
        ++m_softPoolUsed;
    }
}

void Soft::dispatch(VkCommandBuffer cmd, const GpuBody& b, int mode, int n, int groupOffset) {
    int pc[4] = {mode, n, groupOffset, 0};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPl, 0, 1, &b.simSet[m_slot], 0, nullptr);
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

namespace {

int geomVertexCount(const entt::registry& reg, entt::entity e) {
    if (const auto* rend = reg.try_get<Renderable>(e))
        if (const auto* geom = reg.try_get<Geometry>(rend->geom))
            return geom->mesh.vertexCount();
    return 0;
}

}

void Soft::recordPinUpdates(VkCommandBuffer cmd, const entt::registry& reg) {
    // Last frame's sets are no longer in flight (the queue was fence-waited at present): free them so the small
    // pool never exhausts under per-frame re-derivation. Every pool is empty again afterwards; allocations
    // continue from the last one (a fresh pool appears only if a single frame pins more bodies than it holds).
    // This slot's sets are from kRing frames ago: beginFrame waited on this slot's fence, so they're done.
    auto& mine = m_pinSetsBySlot[m_slot];
    for (const PinSet& ps : mine) {
        if (ps.pool >= 0 && (size_t)ps.pool < m_pinPools.size()) {
            vkFreeDescriptorSets(m_dev, m_pinPools[(size_t)ps.pool], 1, &ps.set);
            ++m_pinPoolFree[(size_t)ps.pool];
        }
    }
    mine.clear();
    bool any = false;
    for (const GpuBody& b : m_body)
        if (!b.dead && b.hasPins)
            any = true;
    if (!any || m_pinPipe == VK_NULL_HANDLE)
        return;
    const auto pbinds = storageBinds(3);
    for (const GpuBody& b : m_body) {
        if (b.dead || !b.hasPins)
            continue;
        // Pin targets are re-derived every frame, GPU-local: cloth vertex i takes source vertex pinMap[i] transformed
        // by the anchor node's live world matrix. Default anchor = the body's own node — src holds its local-space
        // rest pose and model is its WorldTransform — so moving the cloth's Transform (or any ancestor) drags the
        // pins with it; no per-frame CPU vertex writes. An explicit pinSrc anchors to another mesh instead:
        // skinned/static meshes store local-space positions and pass their node matrix; soft-body sources are
        // already world-space and pass identity.
        VkBuffer srcPos = b.restPos;
        uint32_t srcN = (uint32_t)b.soft.n;
        Mat4 model(1.0f);
        const SoftBodyData* sb = reg.try_get<SoftBodyData>(b.ent);
        if (!sb)
            continue;
        if (sb->pinSrc != entt::null) {
            // lookup first, then the creating path (geometry uploads lazily on first draw; a source that is
            // never drawn — e.g. headless — would otherwise miss). Success = vertex count > 0: the no-renderer
            // fallback path returns slot -1 with valid buffers
            GpuMeshRef ref = m_vertsFor ? m_vertsFor(sb->pinSrc) : GpuMeshRef{};
            if (ref.rw.pos.count == 0 && m_request) {
                if (const auto* rend = reg.try_get<Renderable>(sb->pinSrc))
                    if (const auto* geom = reg.try_get<Geometry>(rend->geom))
                        ref = m_request(reg, geom->mesh, sb->pinSrc);
            }
            if (ref.rw.pos.count == 0) {
                if (m_pinWarned.insert(sb->pinSrc).second) {
                    const auto* n = reg.try_get<Name>(sb->pinSrc);
                    std::fprintf(stderr, "3dsim: pin source '%s' has no GPU geometry; anchoring to the body's own rest pose\n",
                                 n ? n->id.c_str() : "?");
                }
            } else {
                srcPos = ref.rw.pos.buffer;
                srcN = ref.rw.pos.count;
                if (!reg.try_get<SoftBodyData>(sb->pinSrc))
                    if (const auto* wt = reg.try_get<WorldTransform>(sb->pinSrc))
                        model = wt->toMat4();
            }
        } else if (const auto* wt = reg.try_get<WorldTransform>(b.ent)) {
            model = wt->toMat4();
        }
        // Prefer a pool with free slots (newest first); freed sets return to their own pool.
        int pi = -1;
        for (int i = (int)m_pinPools.size() - 1; i >= 0; --i)
            if (m_pinPoolFree[(size_t)i] > 0) {
                pi = i;
                break;
            }
        if (pi < 0) {
            m_pinPools.push_back(vkMakePool(m_dev, pbinds, (uint32_t)kPoolChunk));
            m_pinPoolFree.push_back(kPoolChunk);
            pi = (int)m_pinPools.size() - 1;
        }
        --m_pinPoolFree[(size_t)pi];
        VkDescriptorSet set;
        const std::vector<VkDescriptorBufferInfo> bi = {
            {srcPos, 0, (VkDeviceSize)12 * srcN},
            {b.pinMap, 0, (VkDeviceSize)4 * b.soft.n},
            {b.pinT, 0, (VkDeviceSize)12 * b.soft.n},
        };
        vkMakeSet(m_dev, m_pinPools[(size_t)pi], m_pinDsl, pbinds, set, bi);
        mine.push_back({pi, set});
        VkBufferMemoryBarrier bmb[3] = {bufBarrier(srcPos), bufBarrier(b.pinMap), bufBarrier(b.pinT)};
        stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, bmb, 3);
        int pc[18] = {}; // mat4 model + nV + srcN (72 bytes)
        std::memcpy(pc, &model, 64);
        pc[16] = b.soft.n;
        pc[17] = (int)srcN;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pinPipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pinPl, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, m_pinPl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
        vkCmdDispatch(cmd, (b.soft.n + 63) / 64, 1, 1);
    }
}

int Soft::setPinWeights(entt::registry& reg, entt::entity e, const std::vector<float>& w) {
    if (!reg.try_get<SoftBodyData>(e))
        return 1;
    const auto it = m_ent.find(e);
    if (it == m_ent.end()) {
        // body not created yet (project() runs later in the frame): validate against the geometry and store;
        // addSoftBody picks the weights up and queues the initial target refresh. An empty array clears pins
        const int n = geomVertexCount(reg, e);
        if (n <= 0)
            return 1;
        if (!w.empty() && (int)w.size() != n)
            return 2;
        reg.get<SoftBodyData>(e).pinW = w;
        return 0;
    }
    GpuBody& b = m_body[(size_t)it->second];
    if (b.dead || (!w.empty() && (int)w.size() != b.soft.n))
        return 2;
    if (m_app)
        m_app->waitAll(); // about to write a buffer in-flight frames' sim sets reference
    std::vector<float> w0(w);
    for (auto& x : w0)
        x = std::clamp(x, 0.0f, 1.0f);
    reg.get<SoftBodyData>(e).pinW = w0;
    if (w0.empty()) {
        // clear: zero the GPU weights so the shader stops holding vertices
        std::vector<float> z(b.soft.n, 0.0f);
        vkWriteBuffer(m_dev, b.pinWMem, z.data(), 4 * (VkDeviceSize)b.soft.n);
        b.hasPins = false;
        return 0;
    }
    vkWriteBuffer(m_dev, b.pinWMem, w0.data(), 4 * (VkDeviceSize)b.soft.n);
    b.hasPins = true; // targets track the anchor's live WorldTransform every frame from here on
    return 0;
}

int Soft::setPinMap(entt::registry& reg, entt::entity e, const std::vector<uint32_t>& map) {
    if (!reg.try_get<SoftBodyData>(e))
        return 1;
    const auto it = m_ent.find(e);
    if (it == m_ent.end()) {
        const int n = geomVertexCount(reg, e);
        if (n <= 0)
            return 1;
        if (!map.empty() && (int)map.size() != n)
            return 2;
        reg.get<SoftBodyData>(e).pinMap = map;
        return 0;
    }
    GpuBody& b = m_body[(size_t)it->second];
    if (b.dead || (!map.empty() && (int)map.size() != b.soft.n))
        return 2;
    if (m_app)
        m_app->waitAll(); // about to write a buffer in-flight frames' sim sets reference
    std::vector<uint32_t> map0(b.soft.n);
    for (int i = 0; i < b.soft.n; ++i)
        map0[i] = (i < (int)map.size()) ? map[i] : (uint32_t)i;
    reg.get<SoftBodyData>(e).pinMap = map;
    vkWriteBuffer(m_dev, b.pinMapMem, map0.data(), 4 * (VkDeviceSize)b.soft.n);
    return 0; // the remap applies on the next soft.update_pins
}

void Soft::setPinSource(entt::registry& reg, entt::entity e, entt::entity src) {
    if (auto* sb = reg.try_get<SoftBodyData>(e))
        sb->pinSrc = src; // takes effect on the next soft.update_pins
}

void Soft::updatePins(entt::entity e) {
    // Kept for API compatibility: pin targets now track their anchor's live WorldTransform every frame,
    // so there is nothing to force.
    (void)e;
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
    if (!m_free.empty() && m_app)
        m_app->waitAll(); // freed buffers/sets may still be in use by up to kSlots-1 in-flight frames
    for (const int id : m_free) {
        GpuBody& b = m_body[(size_t)id];
        freeBody(b);
    }
    m_free.clear();
}

void Soft::record(VkCommandBuffer cmd, const entt::registry& reg, float dt) {
    // Render interpolation (FRAME_PACING.md): snapshot this tick's starting positions so the vertex
    // shader can mix prev→curr with the frame's alpha. Must precede any sim dispatch that writes pos.
    for (const GpuBody& b : m_body) {
        if (b.dead || b.posPrev == VK_NULL_HANDLE)
            continue;
        const VkBufferCopy r{0, 0, 12 * (VkDeviceSize)b.soft.n};
        vkCmdCopyBuffer(cmd, b.pos, b.posPrev, 1, &r);
    }
    recordPinUpdates(cmd, reg);

    for (const GpuBody& b : m_body) {
        if (b.dead)
            continue;
        const SoftBodyData* sb = reg.try_get<SoftBodyData>(b.ent);
        if (!sb)
            continue;
        PhysParams phys{};
        phys.nCaps = m_nCaps;
        phys.dt = dt / (float)sb->steps;
        phys.damping = std::pow(sb->damping, 1.0f / (float)sb->steps);
        phys.gravity = sim::kGravity * sb->mass;
        phys.friction = sb->friction;
        phys.skin = kSkin;
        phys.tension = sb->tension;
        phys.stiff = sb->stiffness;
        phys.maxStep = kSoftMaxStep;
        phys.floorY = m_floorY;
        phys.invSteps = 1.0f / (float)sb->steps;
        vkWriteBuffer(m_dev, b.paramsMem[m_slot], &phys, 44);
    }

    std::vector<VkBufferMemoryBarrier> first;
    for (const GpuBody& b : m_body)
        if (!b.dead)
            for (VkBuffer buf : {b.pos, b.nrm, b.prev, b.sub0, b.contactN, b.contactL, b.tris, b.triStart, b.triList,
                                 b.pinW, b.pinT})
                first.push_back(bufBarrier(buf));
    stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, first.data(),
                 (uint32_t)first.size());

    for (const GpuBody& b : m_body) {
        if (b.dead)
            continue;
        const SoftBodyData* sb = reg.try_get<SoftBodyData>(b.ent);
        if (!sb)
            continue;
        for (int s = 0; s < sb->steps; ++s)
            recordBody(cmd, b, sb->passes, sb->pinned);
    }
    for (const GpuBody& b : m_body) {
        if (b.dead)
            continue;
        const SoftBodyData* sb = reg.try_get<SoftBodyData>(b.ent);
        if (!sb || sb->pinned)
            continue;
        recordMode(cmd, b, 3, b.soft.n, 0);
    }

    for (const GpuBody& b : m_body)
        if (!b.dead) {
            VkBufferMemoryBarrier bmb[3] = {bufBarrier(b.pos), bufBarrier(b.nrm), bufBarrier(b.posPrev)};
            stageBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, bmb, 3);
        }
}

void Soft::freeBody(GpuBody& b) {
    if (b.freed)
        return;
    b.freed = true;
    for (int sl = 0; sl < kRing; ++sl) {
        if (b.simSet[sl] != VK_NULL_HANDLE && b.simPool[sl] >= 0) {
            vkFreeDescriptorSets(m_dev, m_softPools[(size_t)b.simPool[sl]], 1, &b.simSet[sl]);
            b.simSet[sl] = VK_NULL_HANDLE;
            b.simPool[sl] = -1;
        }
        vkFreeBuffer(m_dev, b.paramsBuf[sl], b.paramsMem[sl]);
    }
    vkFreeBuffer(m_dev, b.prev, b.prevMem);
    vkFreeBuffer(m_dev, b.sub0, b.sub0Mem);
    vkFreeBuffer(m_dev, b.pinW, b.pinWMem);
    vkFreeBuffer(m_dev, b.pinT, b.pinTMem);
    vkFreeBuffer(m_dev, b.pinMap, b.pinMapMem);
    vkFreeBuffer(m_dev, b.restPos, b.restPosMem);
    vkFreeBuffer(m_dev, b.posPrev, b.posPrevMem);
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
    vkWriteBuffer(m_dev, m_capsInstancesMem[m_slot], m_caps.data(), 48 * (VkDeviceSize)n);
}

void Soft::reset() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    if (m_app)
        m_app->waitAll(); // about to free everything in-flight frames may still reference
    flushFrees();
    for (auto& b : m_body)
        freeBody(b);
    m_body.clear();
    m_ent.clear();
    m_warned.clear();
    m_free.clear();
    for (auto& p : m_softPools)
        vkFree(m_dev, &p, vkDestroyDescriptorPool);
    m_softPools.clear();
    m_softPoolCap = 0;
    m_softPoolUsed = 0;
    vkFree(m_dev, &m_softDsl, vkDestroyDescriptorSetLayout);
    m_softDsl = VK_NULL_HANDLE;
    vkFree(m_dev, &m_softPl, vkDestroyPipelineLayout);
    m_softPl = VK_NULL_HANDLE;
    vkFree(m_dev, &m_softPipe, vkDestroyPipeline);
    m_softPipe = VK_NULL_HANDLE;
    for (auto& bySlot : m_pinSetsBySlot)
        for (const PinSet& ps : bySlot)
            if (ps.pool >= 0 && (size_t)ps.pool < m_pinPools.size())
                vkFreeDescriptorSets(m_dev, m_pinPools[(size_t)ps.pool], 1, &ps.set);
    for (auto& bySlot : m_pinSetsBySlot)
        bySlot.clear();
    for (auto& p : m_pinPools)
        vkFree(m_dev, &p, vkDestroyDescriptorPool);
    m_pinPools.clear();
    m_pinPoolFree.clear();
    vkFree(m_dev, &m_pinDsl, vkDestroyDescriptorSetLayout);
    m_pinDsl = VK_NULL_HANDLE;
    vkFree(m_dev, &m_pinPl, vkDestroyPipelineLayout);
    m_pinPl = VK_NULL_HANDLE;
    vkFree(m_dev, &m_pinPipe, vkDestroyPipeline);
    m_pinPipe = VK_NULL_HANDLE;
    m_pinWarned.clear();
    for (int sl = 0; sl < kRing; ++sl) {
        vkFreeBuffer(m_dev, m_capsInstances[sl], m_capsInstancesMem[sl]);
        m_capsInstances[sl] = VK_NULL_HANDLE;
        m_capsInstancesMem[sl] = VK_NULL_HANDLE;
    }
    m_caps.clear();
    m_nCaps = 0;
    m_floorY = -std::numeric_limits<float>::infinity();
}

void Soft::shutdown() {
    reset();
    m_dev = VK_NULL_HANDLE;
}
