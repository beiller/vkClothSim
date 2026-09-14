#include "vk/renderer.hpp"

#include "mesh_frag_spv.hpp"
#include "mesh_vert_spv.hpp"
#include "vk/vkapp.hpp"
#include "vk/vkutil.hpp"
#include <imgui_impl_vulkan.h>

namespace {

std::vector<VkDescriptorSetLayoutBinding> meshBinds() {
    return {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
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

} // namespace

void Renderer::init(VkApp& app, int nInstances, const Mat4& viewProj) {
    m_dev = app.device();
    m_pdev = app.pdev();
    m_rp = app.renderPass();
    m_vp = viewProj;

    vkMakeBuffer(m_dev, m_pdev, m_vpUbuf, m_vpMem, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, viewProj.m);
    const auto binds = meshBinds();
    vkMakeDslPool(m_dev, binds, (uint32_t)nInstances + 1, m_dsl, m_pool);
    m_pl = vkMakePipelineLayout(m_dev, m_dsl);
    m_pipe = makeGraphicsPipeline(m_dev, m_rp, mesh_vert_spv, mesh_vert_spv_len / 4, mesh_frag_spv,
                                  mesh_frag_spv_len / 4, m_pl);

    Mesh ground;
    buildGroundMesh(ground);
    const GpuMeshRef groundRef = addMesh(ground);
    addInstance(groundRef.geom);
}

Renderer::GpuMeshRef Renderer::addMesh(const Mesh& mesh) {
    const auto n = (uint32_t)mesh.vertexCount();
    const VkDeviceSize attrSize = (VkDeviceSize)12 * n;
    GpuMesh g;
    g.vtxCount = n;
    g.idxCount = (uint32_t)mesh.indices.size();
    vkMakeBuffer(m_dev, m_pdev, g.pos, g.posMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.pos.data());
    vkMakeBuffer(m_dev, m_pdev, g.nrm, g.nrmMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.nrm.data());
    vkMakeBuffer(m_dev, m_pdev, g.col, g.colMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.col.data());
    vkMakeBuffer(m_dev, m_pdev, g.ibuf, g.ibmem, (VkDeviceSize)g.idxCount * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 mesh.indices.data());
    m_geoms.push_back(g);
    return {(int)m_geoms.size() - 1, MeshGpu{VertexStore{g.pos, g.posMem, n}, VertexStore{g.nrm, g.nrmMem, n}}};
}

int Renderer::addInstance(int geom) {
    const GpuMesh& g = m_geoms[geom];
    InstancedMesh inst;
    inst.geom = geom;
    inst.model = mat4Identity();
    vkMakeBuffer(m_dev, m_pdev, inst.modelUbuf, inst.modelMem, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, inst.model.m);
    const VkDeviceSize attrSize = (VkDeviceSize)12 * g.vtxCount;
    std::vector<VkDescriptorBufferInfo> bi = {
        {g.pos, 0, attrSize}, {g.nrm, 0, attrSize}, {g.col, 0, attrSize}, {m_vpUbuf, 0, 64}, {inst.modelUbuf, 0, 64}};
    vkMakeSet(m_dev, m_pool, m_dsl, meshBinds(), inst.set, bi);
    m_insts.push_back(inst);
    return (int)m_insts.size() - 1;
}

void Renderer::setModel(int inst, const Mat4& model) {
    m_insts[inst].model = model;
}

void Renderer::drawInstance(VkCommandBuffer cmd, VkPipelineLayout pl, const GpuMesh& g, const InstancedMesh& inst) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &inst.set, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, g.ibuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, g.idxCount, 1, 0, 0, 0);
}

void Renderer::draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui) {
    for (auto& inst : m_insts)
        vkWriteBuffer(m_dev, inst.modelMem, inst.model.m, 64);
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
    for (const auto& inst : m_insts)
        drawInstance(cmd, m_pl, m_geoms[inst.geom], inst);
    if (imgui && imgui->CmdLists.Size > 0)
        ImGui_ImplVulkan_RenderDrawData(imgui, cmd);
    vkCmdEndRenderPass(cmd);
    app.submit(cmd);
}

void Renderer::shutdown() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    for (auto& inst : m_insts)
        vkFreeBuffer(m_dev, inst.modelUbuf, inst.modelMem);
    for (auto& g : m_geoms) {
        vkFreeBuffer(m_dev, g.pos, g.posMem);
        vkFreeBuffer(m_dev, g.nrm, g.nrmMem);
        vkFreeBuffer(m_dev, g.col, g.colMem);
        vkFreeBuffer(m_dev, g.ibuf, g.ibmem);
    }
    if (m_pool)
        vkDestroyDescriptorPool(m_dev, m_pool, nullptr);
    if (m_dsl)
        vkDestroyDescriptorSetLayout(m_dev, m_dsl, nullptr);
    if (m_pl)
        vkDestroyPipelineLayout(m_dev, m_pl, nullptr);
    if (m_pipe)
        vkDestroyPipeline(m_dev, m_pipe, nullptr);
    vkFreeBuffer(m_dev, m_vpUbuf, m_vpMem);
}
