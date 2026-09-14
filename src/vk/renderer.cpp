#include "vk/renderer.hpp"

#include "mesh_frag_spv.hpp"
#include "mesh_vert_spv.hpp"
#include "vk/vkapp.hpp"
#include "vk/vkutil.hpp"
#include <algorithm>
#include <cmath>
#include <imgui_impl_vulkan.h>

namespace {

std::vector<VkDescriptorSetLayoutBinding> meshBinds() {
    return {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
    };
}

VkPipeline makeGraphicsPipeline(VkDevice dev, VkRenderPass rp, const void* vsSpv, uint32_t vsLen, const void* fsSpv,
                                uint32_t fsLen, VkPipelineLayout layout) {
    VkShaderModule vs = vkMakeModule(dev, vsSpv, vsLen);
    VkShaderModule fs = vkMakeModule(dev, fsSpv, fsLen);
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
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.minDepthBounds = 0.0f;
    ds.maxDepthBounds = 1.0f;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState ca{};
    ca.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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

void buildGroundMesh(Mesh& m) {
    const float G = 55.0f;
    const float gv[4][3] = {{-G, 0.0f, -G}, {G, 0.0f, -G}, {G, 0.0f, G}, {-G, 0.0f, G}};
    const float COL[3] = {0.19f, 0.21f, 0.17f};
    m.pos.clear();
    m.nrm.clear();
    m.col.clear();
    for (const auto& v : gv) {
        m.pos.insert(m.pos.end(), {v[0], v[1], v[2]});
        m.nrm.insert(m.nrm.end(), {0.0f, 0.0f, 1.0f});
        m.col.insert(m.col.end(), {COL[0], COL[1], COL[2]});
    }
    m.indices = {0, 1, 2, 0, 2, 3};
}

void buildCapsuleBase(std::vector<float>& base, float R, float H) {
    const int S = kCapPhiSegs, M = kCapYRows;
    const float twoPi = 2.0f * kPi;
    base.assign((size_t)(M + 1) * S * 6, 0.0f);
    const float top = H + R, bot = -H - R;
    for (int i = 0; i <= M; ++i) {
        float t = (float)i / M;
        float y = top - t * (top - bot);
        float r, dr, dy;
        if (y >= H)
            dy = y - H;
        else if (y <= -H)
            dy = y + H;
        else
            dy = 0.0f;
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
            size_t v = (size_t)i * S + j;
            base[6 * v + 0] = r * cp;
            base[6 * v + 1] = y;
            base[6 * v + 2] = r * sp;
            base[6 * v + 3] = cp;
            base[6 * v + 4] = -dr;
            base[6 * v + 5] = sp;
        }
    }
}

} // namespace

void Renderer::init(VkApp& app, int nMeshes, const Mat4& viewProj) {
    m_dev = app.device();
    m_pdev = app.pdev();
    m_rp = app.renderPass();
    m_vp = viewProj;

    vkMakeBuffer(m_dev, m_pdev, m_uUbuf, m_uUmem, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, viewProj.m);
    const auto binds = meshBinds();
    vkMakeDslPool(m_dev, binds, (uint32_t)nMeshes + 1, m_dsl, m_pool);
    m_pl = vkMakePipelineLayout(m_dev, m_dsl);
    m_pipe = makeGraphicsPipeline(m_dev, m_rp, mesh_vert_spv, mesh_vert_spv_len / 4, mesh_frag_spv,
                                  mesh_frag_spv_len / 4, m_pl);

    Mesh ground;
    buildGroundMesh(ground);
    const auto gvcount = (uint32_t)ground.vertexCount();
    makeMesh(m_ground, gvcount, ground.pos.data(), ground.nrm.data(), ground.col.data(), ground.indices.data(),
             (uint32_t)ground.indices.size());
}

MeshGpu Renderer::createReadWriteBuffer(const Mesh& mesh) {
    const uint32_t n = (uint32_t)mesh.vertexCount();
    const VkDeviceSize bytes = (VkDeviceSize)12 * n;
    VkBuffer pos, nrm;
    VkDeviceMemory posMem, nrmMem;
    vkMakeBuffer(m_dev, m_pdev, pos, posMem, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.pos.data());
    vkMakeBuffer(m_dev, m_pdev, nrm, nrmMem, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.nrm.data());
    return MeshGpu{VertexStore{pos, posMem, n}, VertexStore{nrm, nrmMem, n}};
}

int Renderer::addMesh(const Mesh& mesh, const MeshGpu& rw) {
    GpuMesh m;
    m.pos = rw.pos.buffer;
    m.posMem = rw.pos.memory;
    m.nrm = rw.nrm.buffer;
    m.nrmMem = rw.nrm.memory;
    m.vtxCount = rw.pos.count;
    m.idxCount = (uint32_t)mesh.indices.size();
    const VkDeviceSize attrSize = (VkDeviceSize)12 * m.vtxCount;
    vkMakeBuffer(m_dev, m_pdev, m.col, m.colMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.col.data());
    vkMakeBuffer(m_dev, m_pdev, m.ibuf, m.ibmem, (VkDeviceSize)m.idxCount * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 mesh.indices.data());
    std::vector<VkDescriptorBufferInfo> bi = {
        {m.pos, 0, attrSize}, {m.nrm, 0, attrSize}, {m.col, 0, attrSize}, {m_uUbuf, 0, 64}};
    vkMakeSet(m_dev, m_pool, m_dsl, meshBinds(), m.set, bi);
    m_soft.push_back(m);
    return (int)m_soft.size() - 1;
}

void Renderer::setCapsuleIndex(int idx) {
    m_capsIdx = idx;
}

void Renderer::makeMesh(GpuMesh& m, uint32_t vtxCount, const float* pos, const float* nrm, const float* col,
                        const uint32_t* idx, uint32_t idxCount) {
    const VkDeviceSize attrSize = (VkDeviceSize)12 * vtxCount;
    vkMakeBuffer(m_dev, m_pdev, m.pos, m.posMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pos);
    vkMakeBuffer(m_dev, m_pdev, m.nrm, m.nrmMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nrm);
    vkMakeBuffer(m_dev, m_pdev, m.col, m.colMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, col);
    m.vtxCount = vtxCount;
    m.idxCount = idxCount;
    vkMakeBuffer(m_dev, m_pdev, m.ibuf, m.ibmem, (VkDeviceSize)idxCount * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, idx);
    std::vector<VkDescriptorBufferInfo> bi = {
        {m.pos, 0, attrSize}, {m.nrm, 0, attrSize}, {m.col, 0, attrSize}, {m_uUbuf, 0, 64}};
    vkMakeSet(m_dev, m_pool, m_dsl, meshBinds(), m.set, bi);
}

void Renderer::bakeCapsules(std::span<const CapsuleGPU> caps) {
    GpuMesh& cm = m_soft[m_capsIdx];
    const V3 capCol{0.85f, 0.35f, 0.30f};
    const size_t vcount = (size_t)caps.size() * kCapVPC;
    void *oPos, *oNrm, *oCol;
    VK(vkMapMemory(m_dev, cm.posMem, 0, (VkDeviceSize)12 * vcount, 0, &oPos));
    VK(vkMapMemory(m_dev, cm.nrmMem, 0, (VkDeviceSize)12 * vcount, 0, &oNrm));
    VK(vkMapMemory(m_dev, cm.colMem, 0, (VkDeviceSize)12 * vcount, 0, &oCol));
    auto* outPos = static_cast<float*>(oPos);
    auto* outNrm = static_cast<float*>(oNrm);
    auto* outCol = static_cast<float*>(oCol);
    for (size_t ci = 0; ci < caps.size(); ++ci) {
        const CapsuleGPU& c = caps[ci];
        float qm[9];
        quatToMat3(c.quat, qm);
        const V3 center{c.centerRadius[0], c.centerRadius[1], c.centerRadius[2]};
        const std::vector<float>& base = baseFor(c.centerRadius[3], c.halfLen[0]);
        const int vbase = (int)(ci * kCapVPC);
        for (uint32_t j = 0; j < kCapVPC; ++j) {
            const int v = vbase + (int)j;
            const float* bp = &base[6 * (size_t)j];
            vStore(outPos, v, vAdd(m3v(qm, {bp[0], bp[1], bp[2]}), center));
            vStore(outNrm, v, m3v(qm, {bp[3], bp[4], bp[5]}));
            vStore(outCol, v, capCol);
        }
    }
    vkUnmapMemory(m_dev, cm.posMem);
    vkUnmapMemory(m_dev, cm.nrmMem);
    vkUnmapMemory(m_dev, cm.colMem);
}

const std::vector<float>& Renderer::baseFor(float radius, float halfLen) {
    for (const auto& b : m_capsBases)
        if (b.params.radius == radius && b.params.halfLen == halfLen)
            return b.v;
    CapsuleBase nb;
    nb.params = {halfLen, radius};
    buildCapsuleBase(nb.v, radius, halfLen);
    m_capsBases.push_back(std::move(nb));
    return m_capsBases.back().v;
}

void Renderer::drawMesh(VkCommandBuffer cmd, VkPipelineLayout pl, const GpuMesh& m) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &m.set, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, m.ibuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m.idxCount, 1, 0, 0, 0);
}

void Renderer::draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui,
                    std::span<const CapsuleGPU> caps) {
    bakeCapsules(caps);
    VkClearValue cv[2]{};
    cv[0].color = {bg[0], bg[1], bg[2], 1.0f};
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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipe);
    drawMesh(cmd, m_pl, m_ground);
    for (const auto& m : m_soft)
        drawMesh(cmd, m_pl, m);
    if (imgui && imgui->CmdLists.Size > 0)
        ImGui_ImplVulkan_RenderDrawData(imgui, cmd);
    vkCmdEndRenderPass(cmd);
    app.submit(cmd);
}

void Renderer::shutdown() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    auto destroyMesh = [this](GpuMesh& m) {
        vkFreeBuffer(m_dev, m.pos, m.posMem);
        vkFreeBuffer(m_dev, m.nrm, m.nrmMem);
        vkFreeBuffer(m_dev, m.col, m.colMem);
        vkFreeBuffer(m_dev, m.ibuf, m.ibmem);
    };
    destroyMesh(m_ground);
    for (auto& m : m_soft)
        destroyMesh(m);
    if (m_pool)
        vkDestroyDescriptorPool(m_dev, m_pool, nullptr);
    if (m_dsl)
        vkDestroyDescriptorSetLayout(m_dev, m_dsl, nullptr);
    if (m_pl)
        vkDestroyPipelineLayout(m_dev, m_pl, nullptr);
    if (m_pipe)
        vkDestroyPipeline(m_dev, m_pipe, nullptr);
    vkFreeBuffer(m_dev, m_uUbuf, m_uUmem);
}
