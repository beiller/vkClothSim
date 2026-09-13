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
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
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

void buildGroundVtx(std::vector<float>& vtx, std::vector<uint32_t>& idx) {
    const float G = 55.0f;
    const float gv[4][3] = {{-G, 0.0f, -G}, {G, 0.0f, -G}, {G, 0.0f, G}, {-G, 0.0f, G}};
    const float COL[3] = {0.19f, 0.21f, 0.17f};
    vtx.clear();
    for (const auto& v : gv) {
        vtx.insert(vtx.end(), {v[0], v[1], v[2], 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, COL[0], COL[1], COL[2], 0.0f});
    }
    idx = {0, 1, 2, 0, 2, 3};
}

void buildCapsuleBase(std::vector<float>& base, std::vector<uint32_t>& idx, int nCaps, float R, float H) {
    const int S = 20, M = 32;
    const int VPC = (M + 1) * S;
    const float twoPi = 2.0f * kPi;
    base.assign((size_t)nCaps * VPC * 6, 0.0f);
    idx.clear();
    for (int ci = 0; ci < nCaps; ++ci) {
        float top = H + R, bot = -H - R;
        uint32_t capBase = (uint32_t)ci * VPC;
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
                size_t v = capBase + (size_t)i * S + j;
                base[6 * v + 0] = r * cp;
                base[6 * v + 1] = y;
                base[6 * v + 2] = r * sp;
                base[6 * v + 3] = cp;
                base[6 * v + 4] = -dr;
                base[6 * v + 5] = sp;
            }
        }
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < S; ++j) {
                uint32_t jn = (j + 1) % S;
                uint32_t a = capBase + (uint32_t)i * S + j;
                uint32_t b = capBase + (uint32_t)i * S + jn;
                uint32_t c = capBase + (uint32_t)(i + 1) * S + j;
                uint32_t d = capBase + (uint32_t)(i + 1) * S + jn;
                idx.push_back(a);
                idx.push_back(b);
                idx.push_back(d);
                idx.push_back(a);
                idx.push_back(d);
                idx.push_back(c);
            }
    }
}

} // namespace

void Renderer::init(VkApp& app, int nCaps, int nSoft, const Mat4& viewProj) {
    m_dev = app.device();
    m_pdev = app.pdev();
    m_rp = app.renderPass();
    m_vp = viewProj;
    m_nCaps = nCaps;

    vkMakeBuffer(m_dev, m_pdev, m_uUbuf, m_uUmem, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, viewProj.m);
    const auto binds = meshBinds();
    vkMakeDslPool(m_dev, binds, (uint32_t)nSoft + 2, m_dsl, m_pool);
    m_pl = vkMakePipelineLayout(m_dev, m_dsl);
    m_pipe = makeGraphicsPipeline(m_dev, m_rp, mesh_vert_spv, mesh_vert_spv_len / 4, mesh_frag_spv,
                                  mesh_frag_spv_len / 4, m_pl);

    std::vector<float> gvtx;
    std::vector<uint32_t> gidx;
    buildGroundVtx(gvtx, gidx);
    vkMakeBuffer(m_dev, m_pdev, m_groundVtx, m_groundVtxMem, (VkDeviceSize)gvtx.size() * 4,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, gvtx.data());
    makeMesh(m_ground, m_groundVtx, m_groundVtxMem, (VkDeviceSize)gvtx.size() * 4, gidx.data(), (uint32_t)gidx.size());

    const int S = 20, M = 32;
    m_vpc = (uint32_t)((M + 1) * S);
    std::vector<uint32_t> cidx;
    buildCapsuleBase(m_capsBase, cidx, m_nCaps, kCapsuleRadius, kCapsuleHalfLen);
    const size_t vcount = (size_t)m_nCaps * m_vpc;
    vkMakeBuffer(m_dev, m_pdev, m_capsVtx, m_capsVtxMem, (VkDeviceSize)48 * vcount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 nullptr);
    makeMesh(m_caps, m_capsVtx, m_capsVtxMem, (VkDeviceSize)48 * vcount, cidx.data(), (uint32_t)cidx.size());
}

void Renderer::makeMesh(GpuMesh& m, VkBuffer vtx, VkDeviceMemory vtxMem, VkDeviceSize vtxSize, const uint32_t* idx,
                        uint32_t idxCount) {
    m.vtx = vtx;
    m.vtxMem = vtxMem;
    m.vtxCount = (uint32_t)(vtxSize / 48);
    m.idxCount = idxCount;
    vkMakeBuffer(m_dev, m_pdev, m.ibuf, m.ibmem, (VkDeviceSize)idxCount * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, idx);
    std::vector<VkDescriptorBufferInfo> bi = {
        {vtx, 0, vtxSize},
        {m_uUbuf, 0, 64},
    };
    vkMakeSet(m_dev, m_pool, m_dsl, meshBinds(), m.set, bi);
}

int Renderer::addMesh(const Mesh& mesh) {
    const auto count = (uint32_t)mesh.vertexCount();
    VkBuffer vtx = VK_NULL_HANDLE;
    VkDeviceMemory vtxMem = VK_NULL_HANDLE;
    vkMakeBuffer(m_dev, m_pdev, vtx, vtxMem, (VkDeviceSize)48 * count, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 mesh.vtx.data());
    GpuMesh m;
    makeMesh(m, vtx, vtxMem, (VkDeviceSize)48 * count, mesh.indices.data(), (uint32_t)mesh.indices.size());
    m_soft.push_back(m);
    return (int)m_soft.size() - 1;
}

VertexStore Renderer::vertexBuffer(int handle) const {
    const GpuMesh& m = m_soft[handle];
    return VertexStore{m.vtx, m.vtxMem, m.vtxCount};
}

void Renderer::bakeCapsules(std::span<const CapsuleGPU> caps) {
    const float COL[3] = {0.85f, 0.35f, 0.30f};
    void* o;
    VK(vkMapMemory(m_dev, m_capsVtxMem, 0, (VkDeviceSize)48 * m_nCaps * m_vpc, 0, &o));
    auto* out = static_cast<float*>(o);
    for (size_t ci = 0; ci < caps.size(); ++ci) {
        const CapsuleGPU& c = caps[ci];
        float qm[9];
        quatToMat3(c.quat, qm);
        V3 center{c.centerRadius[0], c.centerRadius[1], c.centerRadius[2]};
        for (uint32_t j = 0; j < m_vpc; ++j) {
            size_t v = ci * m_vpc + j;
            const float* bp = &m_capsBase[6 * v];
            V3 lp{bp[0], bp[1], bp[2]};
            V3 ln{bp[3], bp[4], bp[5]};
            V3 rp{qm[0] * lp.x + qm[3] * lp.y + qm[6] * lp.z + center.x,
                  qm[1] * lp.x + qm[4] * lp.y + qm[7] * lp.z + center.y,
                  qm[2] * lp.x + qm[5] * lp.y + qm[8] * lp.z + center.z};
            V3 rn{qm[0] * ln.x + qm[3] * ln.y + qm[6] * ln.z, qm[1] * ln.x + qm[4] * ln.y + qm[7] * ln.z,
                  qm[2] * ln.x + qm[5] * ln.y + qm[8] * ln.z};
            float* op = &out[12 * v];
            op[0] = rp.x;
            op[1] = rp.y;
            op[2] = rp.z;
            op[3] = 0.0f;
            op[4] = rn.x;
            op[5] = rn.y;
            op[6] = rn.z;
            op[7] = 0.0f;
            op[8] = COL[0];
            op[9] = COL[1];
            op[10] = COL[2];
            op[11] = 0.0f;
        }
    }
    vkUnmapMemory(m_dev, m_capsVtxMem);
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
    drawMesh(cmd, m_pl, m_caps);
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
    auto db = [this](VkBuffer buf, VkDeviceMemory mem) {
        if (buf) {
            vkDestroyBuffer(m_dev, buf, nullptr);
            vkFreeMemory(m_dev, mem, nullptr);
        }
    };
    auto destroySoftMesh = [this](GpuMesh& m) {
        if (m.vtx) {
            vkDestroyBuffer(m_dev, m.vtx, nullptr);
            vkFreeMemory(m_dev, m.vtxMem, nullptr);
        }
        if (m.ibuf) {
            vkDestroyBuffer(m_dev, m.ibuf, nullptr);
            vkFreeMemory(m_dev, m.ibmem, nullptr);
        }
    };
    auto destroyStaticMesh = [this](GpuMesh& m) {
        if (m.ibuf) {
            vkDestroyBuffer(m_dev, m.ibuf, nullptr);
            vkFreeMemory(m_dev, m.ibmem, nullptr);
        }
    };
    destroyStaticMesh(m_ground);
    destroyStaticMesh(m_caps);
    for (auto& m : m_soft)
        destroySoftMesh(m);
    if (m_pool)
        vkDestroyDescriptorPool(m_dev, m_pool, nullptr);
    if (m_dsl)
        vkDestroyDescriptorSetLayout(m_dev, m_dsl, nullptr);
    if (m_pl)
        vkDestroyPipelineLayout(m_dev, m_pl, nullptr);
    if (m_pipe)
        vkDestroyPipeline(m_dev, m_pipe, nullptr);
    db(m_uUbuf, m_uUmem);
    db(m_groundVtx, m_groundVtxMem);
    db(m_capsVtx, m_capsVtxMem);
}
