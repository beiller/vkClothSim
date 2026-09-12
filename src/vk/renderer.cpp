// renderer.cpp
#include "vk/renderer.hpp"
#include "vk/vkapp.hpp"
#include "vk/vkutil.hpp"
#include "app/scene.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <imgui_impl_vulkan.h>

#include "vk_bg_vert_spv.hpp"
#include "vk_bg_frag_spv.hpp"
#include "caps_vert_spv.hpp"
#include "caps_frag_spv.hpp"
#include "cloth_vert_spv.hpp"
#include "cloth_frag_spv.hpp"
#include "ball_vert_spv.hpp"
#include "ball_frag_spv.hpp"
#include "softbody_spv.hpp"

namespace {

VkShaderModule makeModule(VkDevice dev, const void* code, uint32_t nwords) {
    VkShaderModuleCreateInfo smc{};
    smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smc.codeSize = nwords * sizeof(uint32_t);
    smc.pCode = static_cast<const uint32_t*>(code);
    VkShaderModule m;
    VK(vkCreateShaderModule(dev, &smc, nullptr, &m));
    return m;
}

// Create a graphics pipeline with the shared state: triangle list, no culling, dynamic
// viewport/scissor, no blend, MSAA off. `depthTest` toggles depth test + write (the
// background is drawn without it; the meshes with it). The vertex input (bindings +
// attributes, or none) comes from the caller; the shaders are the baked SPIR-V.
VkPipeline makeGraphicsPipeline(VkDevice dev, VkRenderPass rp,
                                const void* vsSpv, uint32_t vsLen,
                                const void* fsSpv, uint32_t fsLen,
                                VkPipelineLayout layout, bool depthTest,
                                const VkVertexInputBindingDescription* binds, uint32_t nBinds,
                                const VkVertexInputAttributeDescription* attrs, uint32_t nAttrs) {
    VkShaderModule vs = makeModule(dev, vsSpv, vsLen);
    VkShaderModule fs = makeModule(dev, fsSpv, fsLen);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = nBinds;
    vi.pVertexBindingDescriptions = binds;
    vi.vertexAttributeDescriptionCount = nAttrs;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dyn;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depthTest ? VK_TRUE : VK_FALSE;
    if (depthTest) {
        ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        ds.minDepthBounds = 0.0f;
        ds.maxDepthBounds = 1.0f;
    }
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState ca{};
    ca.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    ca.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ca;
    VkGraphicsPipelineCreateInfo gpc{};
    gpc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpc.stageCount = 2;
    gpc.pStages = stages;
    gpc.pVertexInputState = &vi;
    gpc.pInputAssemblyState = &ia;
    gpc.pDynamicState = &dynState;
    gpc.pViewportState = &vp;
    gpc.pRasterizationState = &rs;
    gpc.pMultisampleState = &ms;
    gpc.pDepthStencilState = &ds;
    gpc.pColorBlendState = &cb;
    gpc.layout = layout;
    gpc.renderPass = rp;
    VkPipeline pipe;
    VK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpc, nullptr, &pipe));
    vkDestroyShaderModule(dev, vs, nullptr);
    vkDestroyShaderModule(dev, fs, nullptr);
    return pipe;
}

// Build all capsules' geometry (cylinder + 2 hemispheres) into one VBO/IBO.
// Each vertex: pos(3) nrm(3) capsuleIndex(1) = 7 floats (28 bytes). The ground quad is
// appended in world space, flagged aIdx = -1.
void buildCapsuleGeometry(std::vector<float>& verts, std::vector<uint32_t>& idx, int nCaps) {
    const int S = 20, M = 32;
    const float twoPi = 6.2831853f;
    const float R = 0.5f, H = 0.9f;   // all capsules share these dimensions
    for (int ci = 0; ci < nCaps; ++ci) {
        float top = H + R, bot = -H - R;
        uint32_t capBase = (uint32_t)(verts.size() / 7);
        for (int i = 0; i <= M; ++i) {
            float t = (float)i / M;
            float y = top - t * (top - bot);
            float r, dr;
            float dy = (y >= H) ? (y - H) : ((y <= -H) ? (y + H) : 0.0f);
            if (dy == 0.0f) {
                r = R;
                dr = 0.0f;
            } else {
                r = std::sqrt(std::max(0.0f, R * R - dy * dy));
                dr = (r > 1e-5f) ? -dy / r : 0.0f;
            }
            for (int j = 0; j < S; ++j) {
                float phi = (float)j / S * twoPi;
                float cp = std::cos(phi), sp = std::sin(phi);
                verts.push_back(r * cp);
                verts.push_back(y);
                verts.push_back(r * sp);
                verts.push_back(cp);
                verts.push_back(-dr);
                verts.push_back(sp);
                verts.push_back((float)ci);
            }
        }
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < S; ++j) {
                uint32_t jn = (j + 1) % S;
                uint32_t a = capBase + (uint32_t)i * S + j;
                uint32_t b = capBase + (uint32_t)i * S + jn;
                uint32_t c = capBase + (uint32_t)(i + 1) * S + j;
                uint32_t d = capBase + (uint32_t)(i + 1) * S + jn;
                idx.push_back(a); idx.push_back(b); idx.push_back(d);
                idx.push_back(a); idx.push_back(d); idx.push_back(c);
            }
    }
    // ground quad (world-space, flagged aIdx = -1)
    uint32_t gbase = (uint32_t)(verts.size() / 7);
    float G = 55.0f;
    const float gv[4][3] = {{-G, 0.0f, -G}, {G, 0.0f, -G}, {G, 0.0f, G}, {-G, 0.0f, G}};
    for (auto& v : gv) {
        verts.push_back(v[0]);
        verts.push_back(v[1]);
        verts.push_back(v[2]);
        verts.push_back(0.0f);
        verts.push_back(1.0f);
        verts.push_back(0.0f);
        verts.push_back(-1.0f);
    }
    idx.push_back(gbase); idx.push_back(gbase + 1); idx.push_back(gbase + 2);
    idx.push_back(gbase); idx.push_back(gbase + 2); idx.push_back(gbase + 3);
}

} // namespace

void Renderer::init(VkApp& app, const Scene& scene, const Mat4& viewProj) {
    m_dev = app.device();
    m_pdev = app.pdev();
    m_rp = app.renderPass();
    m_vp = viewProj;
    m_nCaps = scene.rigid().capsuleCount();
    m_clothW = Scene::kCW;
    m_clothH = Scene::kCH;
    initBackground();
    initCapsules();
    initSoftPipeline();
    initSoftBodies(scene);
}

void Renderer::initBackground() {
    VkDevice dev = m_dev;
    // 16-byte color UBO + the full-screen triangle vertex buffer (explicit vertices,
    // robust on all drivers)
    vkMakeBuffer(dev, m_pdev, m_bgUbuf, m_bgUmem, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    float verts[6] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
    vkMakeBuffer(dev, m_pdev, m_bgVbuf, m_bgVmem, sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, verts);

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslc{};
    dslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslc.bindingCount = 1;
    dslc.pBindings = &b;
    VK(vkCreateDescriptorSetLayout(dev, &dslc, nullptr, &m_bgDsl));
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpc{};
    dpc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpc.maxSets = 1;
    dpc.poolSizeCount = 1;
    dpc.pPoolSizes = &ps;
    VK(vkCreateDescriptorPool(dev, &dpc, nullptr, &m_bgPool));
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = m_bgPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &m_bgDsl;
    VK(vkAllocateDescriptorSets(dev, &dsai, &m_bgSet));
    VkDescriptorBufferInfo bi{m_bgUbuf, 0, 16};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = m_bgSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);

    VkPipelineLayoutCreateInfo plc{};
    plc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plc.setLayoutCount = 1;
    plc.pSetLayouts = &m_bgDsl;
    VK(vkCreatePipelineLayout(dev, &plc, nullptr, &m_bgPl));

    VkVertexInputBindingDescription bind{0, 8, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr{0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    m_bgPipe = makeGraphicsPipeline(dev, m_rp,
        vk_bg_vert_spv, vk_bg_vert_spv_len / 4,
        vk_bg_frag_spv, vk_bg_frag_spv_len / 4,
        m_bgPl, /*depthTest=*/false, &bind, 1, &attr, 1);
}

void Renderer::initCapsules() {
    VkDevice dev = m_dev;
    std::vector<float> verts;
    std::vector<uint32_t> idx;
    buildCapsuleGeometry(verts, idx, m_nCaps);
    m_caps.idxCount = (uint32_t)idx.size();
    vkMakeBuffer(dev, m_pdev, m_caps.vbuf, m_caps.vbmem, (VkDeviceSize)verts.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, verts.data());
    vkMakeBuffer(dev, m_pdev, m_caps.ibuf, m_caps.ibmem, (VkDeviceSize)idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, idx.data());
    vkMakeBuffer(dev, m_pdev, m_caps.ubuf, m_caps.ubmem, 80, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    vkMakeBuffer(dev, m_pdev, m_capsInstances, m_capsInstancesMem, 48 * (VkDeviceSize)m_nCaps, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);

    // bindings: 0 viewProj+grid UBO, 1 per-instance capsule SSBO
    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo dslc{};
    dslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslc.bindingCount = 2;
    dslc.pBindings = binds;
    VK(vkCreateDescriptorSetLayout(dev, &dslc, nullptr, &m_caps.dsl));
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dpc{};
    dpc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpc.maxSets = 1;
    dpc.poolSizeCount = 2;
    dpc.pPoolSizes = ps;
    VK(vkCreateDescriptorPool(dev, &dpc, nullptr, &m_caps.pool));
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = m_caps.pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &m_caps.dsl;
    VK(vkAllocateDescriptorSets(dev, &dsai, &m_caps.set));
    VkDescriptorBufferInfo bi[2] = {{m_caps.ubuf, 0, 80}, {m_capsInstances, 0, 48 * (VkDeviceSize)m_nCaps}};
    VkWriteDescriptorSet w[2]{};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = m_caps.set;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &bi[0];
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = m_caps.set;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &bi[1];
    vkUpdateDescriptorSets(dev, 2, w, 0, nullptr);
    VkPipelineLayoutCreateInfo plc{};
    plc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plc.setLayoutCount = 1;
    plc.pSetLayouts = &m_caps.dsl;
    VK(vkCreatePipelineLayout(dev, &plc, nullptr, &m_caps.pl));

    // vertex input: pos(3) nrm(3) capsuleIndex(1) = 28 bytes
    VkVertexInputBindingDescription bind{0, 28, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32_SFLOAT, 24},
    };
    m_caps.pipe = makeGraphicsPipeline(dev, m_rp,
        caps_vert_spv, caps_vert_spv_len / 4,
        caps_frag_spv, caps_frag_spv_len / 4,
        m_caps.pl, /*depthTest=*/true, &bind, 1, attrs, 3);
}

// Soft-body sim tuning: Verlet sub-steps per frame. (The Jacobi relax iterations per
// sub-step = the UI `stiffness`, read each frame.)
constexpr int SOFT_SUBSTEPS = 3;
// Matches the Phys struct in softbody.comp (std430 flat scalars).
struct PhysParams { int nCaps; float dt, damping, gravity, friction, skin, tension, relaxScale; };
static_assert(sizeof(PhysParams) == 32);

void Renderer::initSoftPipeline() {
    VkDevice dev = m_dev;
    // the shared physics params (small SSBO, rewritten each frame)
    vkMakeBuffer(dev, m_pdev, m_physParams, m_physParamsMem, 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    // compute DSL: 0 posA, 1 posB, 2 prev, 3 capsules, 4 entries, 5 entryStart, 6 physParams
    VkDescriptorSetLayoutBinding binds[7]{};
    for (int i = 0; i < 7; ++i) {
        binds[i].binding = i;
        binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binds[i].descriptorCount = 1;
        binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dslc{};
    dslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslc.bindingCount = 7;
    dslc.pBindings = binds;
    VK(vkCreateDescriptorSetLayout(dev, &dslc, nullptr, &m_softDsl));
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 14};   // 7 buffers x 2 bodies
    VkDescriptorPoolCreateInfo dpc{};
    dpc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpc.maxSets = 2;
    dpc.poolSizeCount = 1;
    dpc.pPoolSizes = &ps;
    VK(vkCreateDescriptorPool(dev, &dpc, nullptr, &m_softPool));
    VkPipelineLayoutCreateInfo plc{};
    plc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plc.setLayoutCount = 1;
    plc.pSetLayouts = &m_softDsl;
    VK(vkCreatePipelineLayout(dev, &plc, nullptr, &m_softPl));
    VkShaderModule cm = makeModule(dev, softbody_spv, softbody_spv_len / 4);
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
}

// Create one soft body's sim buffers (posA/posB/prev + the joint entries) from its initial
// vertices + constraints, and its compute descriptor set.
void Renderer::buildBodySimBuffers(GpuBody& b, const sim::SoftBody& body) {
    VkDevice dev = m_dev;
    const int n = b.n;
    // the joint entries: each constraint (a,b) adds an entry to a's list (neighbor b) and to
    // b's list (neighbor a) — the shader relaxes each vertex against its own entries. The
    // layout mirrors the shader's `struct Entry { int j; float rest; float k; int pad; }`
    // (16 B) — `j` MUST be an int in memory (the shader reads it as an int).
    struct EntryBuf { int j; float rest; float k; int pad; };
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
    // the sim buffers (16-byte-stride vec3 positions, matching the render's SSBO)
    std::vector<float> initPos(4 * n);
    for (int i = 0; i < n; ++i) {
        const float* p = body.posPtr(i);
        initPos[4 * i + 0] = p[0]; initPos[4 * i + 1] = p[1]; initPos[4 * i + 2] = p[2]; initPos[4 * i + 3] = 0.0f;
    }
    vkMakeBuffer(dev, m_pdev, b.posA, b.posAMem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, initPos.data());
    vkMakeBuffer(dev, m_pdev, b.posB, b.posBMem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    vkMakeBuffer(dev, m_pdev, b.prev, b.prevMem, (VkDeviceSize)16 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, initPos.data());   // prev = pos (zero velocity)
    b.initPos = std::move(initPos);   // keep a CPU copy for reset
    vkMakeBuffer(dev, m_pdev, b.entries, b.entriesMem, (VkDeviceSize)16 * b.nEntries, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, entries.data());
    vkMakeBuffer(dev, m_pdev, b.entryStart, b.entryStartMem, (VkDeviceSize)4 * (n + 1), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, start.data());
    // the compute descriptor set (this body's buffers + the shared capsules + phys params)
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = m_softPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &m_softDsl;
    VK(vkAllocateDescriptorSets(dev, &dsai, &b.simSet));
    VkDescriptorBufferInfo bi[7] = {
        {b.posA, 0, (VkDeviceSize)16 * n},
        {b.posB, 0, (VkDeviceSize)16 * n},
        {b.prev, 0, (VkDeviceSize)16 * n},
        {m_capsInstances, 0, (VkDeviceSize)48 * m_nCaps},
        {b.entries, 0, (VkDeviceSize)16 * b.nEntries},
        {b.entryStart, 0, (VkDeviceSize)4 * (n + 1)},
        {m_physParams, 0, 32},
    };
    VkWriteDescriptorSet w[7]{};
    for (int i = 0; i < 7; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = b.simSet;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[i].pBufferInfo = &bi[i];
    }
    vkUpdateDescriptorSets(dev, 7, w, 0, nullptr);
}

// Build both GPU soft bodies (the cloth + the ball): their sim buffers + the render meshes.
void Renderer::initSoftBodies(const Scene& scene) {
    VkDevice dev = m_dev;
    {   // the cloth: a grid mesh (render) + its joints
        GpuBody& b = m_cloth;
        b.n = scene.cloth().numVertices();
        buildBodySimBuffers(b, scene.cloth());
        std::vector<uint32_t> idx;
        for (int gy = 0; gy < m_clothH - 1; ++gy)
            for (int gx = 0; gx < m_clothW - 1; ++gx) {
                uint32_t a = (uint32_t)(gy * m_clothW + gx);
                idx.push_back(a); idx.push_back(a + 1); idx.push_back(a + m_clothW);
                idx.push_back(a + 1); idx.push_back(a + m_clothW + 1); idx.push_back(a + m_clothW);
            }
        b.mesh.idxCount = (uint32_t)idx.size();
        b.mesh.sbuf = b.posA; b.mesh.sbmem = b.posAMem;   // render reads the current buffer
        vkMakeBuffer(dev, m_pdev, b.mesh.ibuf, b.mesh.ibmem, (VkDeviceSize)idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, idx.data());
        vkMakeBuffer(dev, m_pdev, b.mesh.ubuf, b.mesh.ubmem, 80, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
        makePosUboDescriptorSet(dev, b.mesh, (VkDeviceSize)16 * b.n);
        b.mesh.pipe = makeGraphicsPipeline(dev, m_rp,
            cloth_vert_spv, cloth_vert_spv_len / 4,
            cloth_frag_spv, cloth_frag_spv_len / 4,
            b.mesh.pl, /*depthTest=*/true, nullptr, 0, nullptr, 0);
        void* up;
        VK(vkMapMemory(dev, b.mesh.ubmem, 0, 80, 0, &up));
        std::memcpy(up, m_vp.m, 64);
        std::memcpy((char*)up + 64, &m_clothW, 4);
        std::memcpy((char*)up + 68, &m_clothH, 4);
        vkUnmapMemory(dev, b.mesh.ubmem);
    }
    {   // the ball: a sphere mesh (render) + its joints (no pressure — a plain soft body)
        GpuBody& b = m_ball;
        b.n = scene.ball().numVertices();
        buildBodySimBuffers(b, scene.ball());
        std::vector<uint32_t> idx;
        for (auto& t : scene.ballTris()) { idx.push_back(t.a); idx.push_back(t.b); idx.push_back(t.c); }
        b.mesh.idxCount = (uint32_t)idx.size();
        b.mesh.sbuf = b.posA; b.mesh.sbmem = b.posAMem;
        vkMakeBuffer(dev, m_pdev, b.mesh.ibuf, b.mesh.ibmem, (VkDeviceSize)idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, idx.data());
        vkMakeBuffer(dev, m_pdev, b.mesh.ubuf, b.mesh.ubmem, 80, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
        makePosUboDescriptorSet(dev, b.mesh, (VkDeviceSize)16 * b.n);
        b.mesh.pipe = makeGraphicsPipeline(dev, m_rp,
            ball_vert_spv, ball_vert_spv_len / 4,
            ball_frag_spv, ball_frag_spv_len / 4,
            b.mesh.pl, /*depthTest=*/true, nullptr, 0, nullptr, 0);
        void* up;
        VK(vkMapMemory(dev, b.mesh.ubmem, 0, 80, 0, &up));
        std::memcpy(up, m_vp.m, 64);
        float c0[3] = {0, 0, 0};
        std::memcpy((char*)up + 64, c0, 12);   // center unused (the normal is a derivative)
        vkUnmapMemory(dev, b.mesh.ubmem);
    }
}

// The cloth + ball share the same descriptor layout (0: pos SSBO, 1: UBO): create the
// descriptor set layout + pool + set + pipeline layout for one of them.
void Renderer::makePosUboDescriptorSet(VkDevice dev, Mesh& m, VkDeviceSize posRange) {
    VkDescriptorSetLayoutBinding rb[2]{};
    rb[0].binding = 0;
    rb[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    rb[0].descriptorCount = 1;
    rb[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    rb[1].binding = 1;
    rb[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    rb[1].descriptorCount = 1;
    rb[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo rslc{};
    rslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    rslc.bindingCount = 2;
    rslc.pBindings = rb;
    VK(vkCreateDescriptorSetLayout(dev, &rslc, nullptr, &m.dsl));
    VkDescriptorPoolSize rps[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo rpdc{};
    rpdc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    rpdc.maxSets = 1;
    rpdc.poolSizeCount = 2;
    rpdc.pPoolSizes = rps;
    VK(vkCreateDescriptorPool(dev, &rpdc, nullptr, &m.pool));
    VkDescriptorSetAllocateInfo rdsai{};
    rdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    rdsai.descriptorPool = m.pool;
    rdsai.descriptorSetCount = 1;
    rdsai.pSetLayouts = &m.dsl;
    VK(vkAllocateDescriptorSets(dev, &rdsai, &m.set));
    VkDescriptorBufferInfo rbi[2] = {{m.sbuf, 0, posRange}, {m.ubuf, 0, 80}};
    VkWriteDescriptorSet rw[2]{};
    rw[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rw[0].dstSet = m.set;
    rw[0].dstBinding = 0;
    rw[0].descriptorCount = 1;
    rw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    rw[0].pBufferInfo = &rbi[0];
    rw[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rw[1].dstSet = m.set;
    rw[1].dstBinding = 1;
    rw[1].descriptorCount = 1;
    rw[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    rw[1].pBufferInfo = &rbi[1];
    vkUpdateDescriptorSets(dev, 2, rw, 0, nullptr);
    VkPipelineLayoutCreateInfo rplc{};
    rplc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    rplc.setLayoutCount = 1;
    rplc.pSetLayouts = &m.dsl;
    VK(vkCreatePipelineLayout(dev, &rplc, nullptr, &m.pl));
}

// Point a body's render descriptor at the position buffer it is currently in (posA or posB).
void Renderer::updateRenderSet(GpuBody& b) {
    VkBuffer cur = b.inA ? b.posA : b.posB;
    b.mesh.sbuf = cur;
    VkDescriptorBufferInfo bi{cur, 0, (VkDeviceSize)16 * b.n};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = b.mesh.set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(m_dev, 1, &w, 0, nullptr);
}

// Record one soft-body dispatch (barrier + pipeline + descriptors + push constants +
// dispatch) and flip inA (the write went to the other buffer).
void Renderer::recordDispatch(VkCommandBuffer cmd, GpuBody& b, int mode) {
    VkBuffer rd = b.inA ? b.posA : b.posB;
    VkBuffer bufs[2] = {rd, b.prev};
    VkBufferMemoryBarrier bmb[2]{};
    for (int i = 0; i < 2; ++i) {
        bmb[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bmb[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bmb[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bmb[i].buffer = bufs[i];
        bmb[i].offset = 0;
        bmb[i].size = VK_WHOLE_SIZE;
        bmb[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bmb[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 2, bmb, 0, nullptr);
    int pc[4] = {mode, b.inA ? 1 : 0, b.n, 0};   // mode, readFromA, n, pinned(unused)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_softPl, 0, 1, &b.simSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_softPl, VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)sizeof(pc), pc);
    vkCmdDispatch(cmd, (b.n + 63) / 64, 1, 1);
    b.inA = !b.inA;
}

// Record one body's full frame of sim (substeps x {Verlet + relax + collide}) into cmd. A
// held (pinned) body is skipped entirely (frozen at its initial positions, no inA flips).
void Renderer::recordBody(VkCommandBuffer cmd, GpuBody& b, int relaxIters, int pinned) {
    if (pinned) return;
    for (int s = 0; s < SOFT_SUBSTEPS; ++s) {
        recordDispatch(cmd, b, 0);                    // Verlet
        for (int k = 0; k < relaxIters; ++k)
            recordDispatch(cmd, b, 1);                // joint relaxation (Jacobi)
        recordDispatch(cmd, b, 2);                    // collision
    }
}

// Record the soft-body sim (cloth + ball) into cmd: set the shared phys params, dispatch both
// bodies, and point both render descriptors at the current position buffers.
void Renderer::recordSoftSim(VkCommandBuffer cmd, const SimParams& p, int clothPinned) {
    PhysParams phys;
    phys.nCaps = m_nCaps;
    phys.dt = (1.0f / 60.0f) / SOFT_SUBSTEPS;
    phys.damping = p.damping;
    phys.gravity = -9.81f * p.mass;
    phys.friction = 0.1f;
    phys.skin = 0.01f;
    phys.tension = p.tension;
    phys.relaxScale = m_relaxScale;
    void* up;
    VK(vkMapMemory(m_dev, m_physParamsMem, 0, 32, 0, &up));
    std::memcpy(up, &phys, 32);
    vkUnmapMemory(m_dev, m_physParamsMem);
    // the relax iterations per sub-step = the UI stiffness (the same value the CPU solver used)
    recordBody(cmd, m_cloth, p.stiffness, clothPinned);
    recordBody(cmd, m_ball, p.stiffness, 0);
    updateRenderSet(m_cloth);
    updateRenderSet(m_ball);
}

// --shot helper: record + submit + wait ONE sim step (no render).
void Renderer::stepSoftFrame(VkApp& app, const SimParams& p, int clothPinned, float relaxScale) {
    m_relaxScale = relaxScale;
    VkCommandBuffer cmd = app.beginCommands();
    recordSoftSim(cmd, p, clothPinned);
    app.submit(cmd);
}

// Read back a body's current positions ("cloth"/"ball") for --shot diagnostics.
void Renderer::readbackBody(VkApp& app, const char* name, std::vector<float>& out) {
    GpuBody& b = (std::string(name) == "cloth") ? m_cloth : m_ball;
    VkBuffer cur = b.inA ? b.posA : b.posB;
    VkDeviceSize size = (VkDeviceSize)16 * b.n;
    VkBuffer staging;
    VkDeviceMemory smem;
    vkMakeBuffer(m_dev, m_pdev, staging, smem, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr);
    VkCommandBuffer cmd = app.beginCommands();
    VkBufferCopy rc{0, 0, size};
    vkCmdCopyBuffer(cmd, cur, staging, 1, &rc);
    app.submit(cmd);   // synchronous (waits)
    void* p;
    VK(vkMapMemory(m_dev, smem, 0, size, 0, &p));
    out.resize(4 * b.n);
    std::memcpy(out.data(), p, size);
    vkUnmapMemory(m_dev, smem);
    vkDestroyBuffer(m_dev, staging, nullptr);
    vkFreeMemory(m_dev, smem, nullptr);
}

// Re-upload one body's initial state to the GPU (posA + prev = initial, inA = true). The GPU
// is idle here (synchronous submit), so the host-visible buffers can be rewritten directly.
void Renderer::resetBody(GpuBody& b) {
    void* pa;
    VK(vkMapMemory(m_dev, b.posAMem, 0, (VkDeviceSize)16 * b.n, 0, &pa));
    std::memcpy(pa, b.initPos.data(), (size_t)16 * b.n);
    vkUnmapMemory(m_dev, b.posAMem);
    void* pv;
    VK(vkMapMemory(m_dev, b.prevMem, 0, (VkDeviceSize)16 * b.n, 0, &pv));
    std::memcpy(pv, b.initPos.data(), (size_t)16 * b.n);
    vkUnmapMemory(m_dev, b.prevMem);
    b.inA = true;
}

void Renderer::resetSoftBodies() {
    resetBody(m_cloth);
    resetBody(m_ball);
}

void Renderer::resetBall() {
    resetBody(m_ball);
}

void Renderer::uploadCapsules(const std::vector<CapsuleGPU>& instances) {
    void* p;
    VK(vkMapMemory(m_dev, m_capsInstancesMem, 0, 48 * instances.size(), 0, &p));
    std::memcpy(p, instances.data(), 48 * instances.size());
    vkUnmapMemory(m_dev, m_capsInstancesMem);
    // viewProj + the cloth grid dims into the capsule + cloth UBOs (the shaders read what
    // they need: the capsule shader just viewProj, the cloth shader viewProj+W+H). The ball's
    // UBO was set once at init (viewProj; its center is unused).
    uploadGridUbo(m_caps);
    uploadGridUbo(m_cloth.mesh);
}

// viewProj + the cloth grid dims (W,H) into a mesh's 80-byte UBO.
void Renderer::uploadGridUbo(Mesh& m) {
    void* up;
    VK(vkMapMemory(m_dev, m.ubmem, 0, 80, 0, &up));
    std::memcpy(up, m_vp.m, 64);
    std::memcpy((char*)up + 64, &m_clothW, 4);
    std::memcpy((char*)up + 68, &m_clothH, 4);
    vkUnmapMemory(m_dev, m.ubmem);
}


void Renderer::drawMesh(VkCommandBuffer cmd, const Mesh& m, VkBuffer* vbuf) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m.pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m.pl, 0, 1, &m.set, 0, nullptr);
    VkDeviceSize zero = 0;
    if (vbuf)
        vkCmdBindVertexBuffers(cmd, 0, 1, vbuf, &zero);
    vkCmdBindIndexBuffer(cmd, m.ibuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m.idxCount, 1, 0, 0, 0);
}

void Renderer::draw(VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui,
                    bool doSim, const SimParams& p, int clothPinned) {
    // the background color (a UI param)
    {
        void* p;
        VK(vkMapMemory(m_dev, m_bgUmem, 0, 16, 0, &p));
        float col[4] = {bg[0], bg[1], bg[2], 1.0f};
        std::memcpy(p, col, 16);
        vkUnmapMemory(m_dev, m_bgUmem);
    }

    VkCommandBuffer cmd = app.beginCommands();
    if (doSim)   // the GPU soft-body sim runs before the render pass (same command buffer)
        recordSoftSim(cmd, p, clothPinned);
    VkClearValue cv[2]{};
    cv[0].color = {0.04f, 0.04f, 0.05f, 1.0f};
    cv[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rpb{};
    rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpb.renderPass = app.renderPass();
    rpb.framebuffer = app.frameBuffer(fb);
    rpb.renderArea = {{0, 0}, app.extent()};
    rpb.clearValueCount = 2;
    rpb.pClearValues = cv;
    vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);
    VkExtent2D ext = app.extent();
    VkViewport vpt{0, (float)ext.height, (float)ext.width, -(float)ext.height, 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vpt);
    VkRect2D sc{0, 0, ext.width, ext.height};
    vkCmdSetScissor(cmd, 0, 1, &sc);
    // the full-screen background
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bgPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bgPl, 0, 1, &m_bgSet, 0, nullptr);
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_bgVbuf, &zero);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    // the capsules (+ the ground quad in the same VBO/IBO), the cloth, the ball
    drawMesh(cmd, m_caps, &m_caps.vbuf);
    drawMesh(cmd, m_cloth.mesh, nullptr);
    drawMesh(cmd, m_ball.mesh, nullptr);
    // the ImGui overlay (the same swapchain image; zero CPU pixel copy)
    if (imgui && imgui->CmdLists.Size > 0)
        ImGui_ImplVulkan_RenderDrawData(imgui, cmd);
    vkCmdEndRenderPass(cmd);
    app.submit(cmd);
}

void Renderer::shutdown() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    auto destroyMesh = [this](Mesh& m) {
        if (m.pipe)
            vkDestroyPipeline(m_dev, m.pipe, nullptr);
        if (m.pl)
            vkDestroyPipelineLayout(m_dev, m.pl, nullptr);
        if (m.pool)
            vkDestroyDescriptorPool(m_dev, m.pool, nullptr);
        if (m.dsl)
            vkDestroyDescriptorSetLayout(m_dev, m.dsl, nullptr);
        if (m.vbuf) {
            vkDestroyBuffer(m_dev, m.vbuf, nullptr);
            vkFreeMemory(m_dev, m.vbmem, nullptr);
        }
        if (m.sbuf) {
            vkDestroyBuffer(m_dev, m.sbuf, nullptr);
            vkFreeMemory(m_dev, m.sbmem, nullptr);
        }
        if (m.ibuf) {
            vkDestroyBuffer(m_dev, m.ibuf, nullptr);
            vkFreeMemory(m_dev, m.ibmem, nullptr);
        }
        if (m.ubuf) {
            vkDestroyBuffer(m_dev, m.ubuf, nullptr);
            vkFreeMemory(m_dev, m.ubmem, nullptr);
        }
    };
    destroyMesh(m_caps);
    // the GPU soft bodies: the render mesh (its sbuf aliases a sim buffer, so skip it there)
    // + the sim buffers (posA/posB/prev/entries/entryStart) + the sim descriptor set (freed
    // with m_softPool below).
    auto destroyBody = [this, &destroyMesh](GpuBody& b) {
        b.mesh.sbuf = VK_NULL_HANDLE;
        destroyMesh(b.mesh);
        auto db = [this](VkBuffer buf, VkDeviceMemory mem) {
            if (buf) { vkDestroyBuffer(m_dev, buf, nullptr); vkFreeMemory(m_dev, mem, nullptr); }
        };
        db(b.posA, b.posAMem);
        db(b.posB, b.posBMem);
        db(b.prev, b.prevMem);
        db(b.entries, b.entriesMem);
        db(b.entryStart, b.entryStartMem);
    };
    destroyBody(m_cloth);
    destroyBody(m_ball);
    if (m_softPool)
        vkDestroyDescriptorPool(m_dev, m_softPool, nullptr);
    if (m_softDsl)
        vkDestroyDescriptorSetLayout(m_dev, m_softDsl, nullptr);
    if (m_softPl)
        vkDestroyPipelineLayout(m_dev, m_softPl, nullptr);
    if (m_softPipe)
        vkDestroyPipeline(m_dev, m_softPipe, nullptr);
    if (m_physParams) {
        vkDestroyBuffer(m_dev, m_physParams, nullptr);
        vkFreeMemory(m_dev, m_physParamsMem, nullptr);
    }
    if (m_capsInstances) {
        vkDestroyBuffer(m_dev, m_capsInstances, nullptr);
        vkFreeMemory(m_dev, m_capsInstancesMem, nullptr);
    }
    if (m_bgPipe)
        vkDestroyPipeline(m_dev, m_bgPipe, nullptr);
    if (m_bgPl)
        vkDestroyPipelineLayout(m_dev, m_bgPl, nullptr);
    if (m_bgPool)
        vkDestroyDescriptorPool(m_dev, m_bgPool, nullptr);
    if (m_bgDsl)
        vkDestroyDescriptorSetLayout(m_dev, m_bgDsl, nullptr);
    if (m_bgUbuf) {
        vkDestroyBuffer(m_dev, m_bgUbuf, nullptr);
        vkFreeMemory(m_dev, m_bgUmem, nullptr);
    }
    if (m_bgVbuf) {
        vkDestroyBuffer(m_dev, m_bgVbuf, nullptr);
        vkFreeMemory(m_dev, m_bgVmem, nullptr);
    }
}
