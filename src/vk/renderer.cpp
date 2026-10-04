#include "vk/renderer.hpp"

#include "env_cube_spv.hpp"
#include "env_irradiance_spv.hpp"
#include "env_prefilter_spv.hpp"
#include "mesh_frag_spv.hpp"
#include "mesh_vert_spv.hpp"
#include "shadow_frag_spv.hpp"
#include "shadow_vert_spv.hpp"
#include "tonemap_frag_spv.hpp"
#include "tonemap_vert_spv.hpp"
#include "vk/vkapp.hpp"
#include "vk/vkutil.hpp"
#include <cstdlib>
#include <imgui_impl_vulkan.h>

namespace {

struct ModelUbo {
    float model[16];
    float baseColor[3];
    float metallic;
    float roughness;
    float pad[3];
};
static_assert(sizeof(ModelUbo) == 96, "ModelUbo must match the shader std430 layout");

struct ViewUbo {
    float viewProj[16];
    float camPos[3];
    float envIntensity;
    float shadowTexels;
    float pad[2];
};
static_assert(sizeof(ViewUbo) == 92, "ViewUbo must match the shader std430 layout");

struct LightGPU {
    float pos[3];
    float intensity;
    float color[3];
    float radius;
    float on;
    float shadowNear;
    float shadowFar;
    float shadowNormalBias;
    float biasBase;
    float biasSlope;
    float searchScale;
    float maxRadius;
};
static_assert(sizeof(LightGPU) == 64, "LightGPU must match the shader std430 layout");

struct LightsUbo {
    int count;
    float pad[3];
    LightGPU lights[Renderer::kMaxLights];
};
static_assert(sizeof(LightsUbo) == 16 + Renderer::kMaxLights * 64, "LightsUbo must match the shader std430 layout");

ModelUbo packModel(const Mat4& model, const V3& baseColor, float metallic, float roughness) {
    ModelUbo mb{};
    std::memcpy(mb.model, model.m, 64);
    mb.baseColor[0] = baseColor.x;
    mb.baseColor[1] = baseColor.y;
    mb.baseColor[2] = baseColor.z;
    mb.metallic = metallic;
    mb.roughness = roughness;
    return mb;
}

std::vector<VkDescriptorSetLayoutBinding> meshBinds() {
    return {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {13, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    };
}

std::vector<VkDescriptorSetLayoutBinding> tonemapBinds() {
    return {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    };
}

VkPipeline makeGraphicsPipeline(VkDevice dev, VkRenderPass rp, const void* vsSpv, uint32_t vsLen, const void* fsSpv,
                                uint32_t fsLen, VkPipelineLayout layout, bool depth, bool cullBack = false) {
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
    rs.cullMode = cullBack ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = depth ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depth ? VK_TRUE : VK_FALSE;
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

} // namespace

void Renderer::init(VkApp& app, int nInstances, const Mat4& viewProj) {
    m_dev = app.device();
    m_pdev = app.pdev();
    m_rp = app.renderPass();

    m_viewProj = viewProj;
    ViewUbo vpInit{};
    std::memcpy(vpInit.viewProj, viewProj.m, 64);
    vkMakeBuffer(m_dev, m_pdev, m_vpUbuf, m_vpMem, sizeof(ViewUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &vpInit);
    vkMakeBuffer(m_dev, m_pdev, m_lightsUbuf, m_lightsMem, sizeof(LightsUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 nullptr);
    const auto binds = meshBinds();
    vkMakeDslPool(m_dev, binds, (uint32_t)nInstances, m_dsl, m_pool);
    m_pl = vkMakePipelineLayout(m_dev, m_dsl);
    m_pipe = makeGraphicsPipeline(m_dev, app.sceneRenderPass(), mesh_vert_spv, mesh_vert_spv_len / 4, mesh_frag_spv,
                                  mesh_frag_spv_len / 4, m_pl, true);

    const uint8_t white[4] = {255, 255, 255, 255};
    vkMakeImage2D(m_dev, m_pdev, 1, 1, white, m_white.img, m_white.mem, m_white.view);
    vkMakeSampler(m_dev, VK_SAMPLER_ADDRESS_MODE_REPEAT, m_texSampler);

    vkMakeBuffer(m_dev, m_pdev, m_tmUbuf, m_tmMem, 32, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    const auto tmBinds = tonemapBinds();
    vkMakeDslPool(m_dev, tmBinds, 2, m_tmDsl, m_tmPool);
    m_tmPl = vkMakePipelineLayout(m_dev, m_tmDsl);
    m_tmPipe = makeGraphicsPipeline(m_dev, app.renderPass(), tonemap_vert_spv, tonemap_vert_spv_len / 4,
                                    tonemap_frag_spv, tonemap_frag_spv_len / 4, m_tmPl, false);
    VkAttachmentDescription nca{};
    nca.format = app.swapchainFormat();
    nca.samples = VK_SAMPLE_COUNT_1_BIT;
    nca.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    nca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    nca.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    nca.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    nca.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    nca.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ncr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription ncs{};
    ncs.colorAttachmentCount = 1;
    ncs.pColorAttachments = &ncr;
    VkRenderPassCreateInfo nrpc{};
    nrpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    nrpc.attachmentCount = 1;
    nrpc.pAttachments = &nca;
    nrpc.subpassCount = 1;
    nrpc.pSubpasses = &ncs;
    VK(vkCreateRenderPass(m_dev, &nrpc, nullptr, &m_noClearRp));
    rebuildTonemapSet(app);
    initShadow(app);
}

Renderer::GpuMeshRef Renderer::addMesh(const Mesh& mesh) {
    const auto n = (uint32_t)mesh.vertexCount();
    const VkDeviceSize attrSize = (VkDeviceSize)12 * n;
    GpuMesh g;
    g.vtxCount = n;
    g.idxCount = (uint32_t)mesh.indices.size();
    vkMakeBuffer(m_dev, m_pdev, g.pos, g.posMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.pos.data());
    vkMakeBuffer(m_dev, m_pdev, g.nrm, g.nrmMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.nrm.data());
    vkMakeBuffer(m_dev, m_pdev, g.uv, g.uvMem, (VkDeviceSize)8 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.uv.data());
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
    const ModelUbo modelInit = packModel(inst.model, inst.baseColor, inst.metallic, inst.roughness);
    vkMakeBuffer(m_dev, m_pdev, inst.modelUbuf, inst.modelMem, sizeof(ModelUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 &modelInit);
    const VkDeviceSize attrSize = (VkDeviceSize)12 * g.vtxCount;
    std::vector<VkDescriptorBufferInfo> bi = {{g.pos, 0, attrSize},
                                              {g.nrm, 0, attrSize},
                                              {m_vpUbuf, 0, sizeof(ViewUbo)},
                                              {inst.modelUbuf, 0, sizeof(ModelUbo)},
                                              {g.uv, 0, (VkDeviceSize)8 * g.vtxCount}};
    vkMakeSet(m_dev, m_pool, m_dsl, meshBinds(), inst.set, bi);
    m_insts.push_back(inst);
    const int id = (int)m_insts.size() - 1;
    setTexture(id, 0, -1);
    setTexture(id, 1, -1);
    setTexture(id, 2, -1);
    VkDescriptorBufferInfo lb{m_lightsUbuf, 0, sizeof(LightsUbo)};
    VkWriteDescriptorSet sw[2]{};
    sw[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sw[0].dstSet = inst.set;
    sw[0].dstBinding = 12;
    sw[0].descriptorCount = 1;
    sw[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sw[0].pImageInfo = &m_shadowInfo;
    sw[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sw[1].dstSet = inst.set;
    sw[1].dstBinding = 13;
    sw[1].descriptorCount = 1;
    sw[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sw[1].pBufferInfo = &lb;
    vkUpdateDescriptorSets(m_dev, 2, sw, 0, nullptr);
    if (m_envReady)
        writeEnvSet(inst.set);
    return id;
}

void Renderer::setModel(int inst, const Mat4& model) {
    m_insts[inst].model = model;
}

void Renderer::setMaterial(int inst, const V3& baseColor, float metallic, float roughness) {
    m_insts[inst].baseColor = baseColor;
    m_insts[inst].metallic = metallic;
    m_insts[inst].roughness = roughness;
}

int Renderer::addTexture(const void* rgba, uint32_t w, uint32_t h) {
    GpuTexture t;
    vkMakeImage2D(m_dev, m_pdev, w, h, rgba, t.img, t.mem, t.view);
    m_texs.push_back(t);
    return (int)m_texs.size() - 1;
}

void Renderer::setTexture(int inst, int channel, int texId) {
    const bool hasTex = texId >= 0 && (size_t)texId < m_texs.size();
    const GpuTexture& t = hasTex ? m_texs[texId] : m_white;
    VkDescriptorImageInfo ii;
    ii.sampler = m_texSampler;
    ii.imageView = t.view;
    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = m_insts[inst].set;
    w.dstBinding = 6 + channel;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(m_dev, 1, &w, 0, nullptr);
}

void Renderer::setViewProj(const Mat4& vp, const V3& camPos) {
    m_viewProj = vp;
    m_camPos = camPos;
}

void Renderer::setLights(const std::vector<Light>& lights) {
    m_lights = lights;
}

void Renderer::setEnvIntensity(float intensity) {
    m_envIntensity = intensity;
}

void Renderer::writeViewUbo(const Mat4& vp, const V3& cam) {
    ViewUbo u{};
    std::memcpy(u.viewProj, vp.m, 64);
    u.camPos[0] = cam.x;
    u.camPos[1] = cam.y;
    u.camPos[2] = cam.z;
    u.envIntensity = m_envIntensity;
    u.shadowTexels = (float)m_shadowSize;
    vkWriteBuffer(m_dev, m_vpMem, &u, sizeof(ViewUbo));
}

void Renderer::prepBuffers() {
    writeLights();
    for (auto& inst : m_insts) {
        const ModelUbo mb = packModel(inst.model, inst.baseColor, inst.metallic, inst.roughness);
        vkWriteBuffer(m_dev, inst.modelMem, &mb, sizeof(ModelUbo));
    }
}

void Renderer::renderScenePass(VkCommandBuffer cmd, VkApp& app, const float bg[3]) {
    const VkExtent2D ext = app.extent();
    VkViewport vpt{0, (float)ext.height, (float)ext.width, -(float)ext.height, 0, 1};
    VkRect2D sc{0, 0, ext.width, ext.height};
    VkClearValue scv[2]{};
    scv[0].color = {bg[0], bg[1], bg[2], 1.0f};
    scv[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo spb{};
    spb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    spb.renderPass = app.sceneRenderPass();
    spb.framebuffer = app.sceneFramebuffer();
    spb.renderArea = {{0, 0}, ext};
    spb.clearValueCount = 2;
    spb.pClearValues = scv;
    vkCmdBeginRenderPass(cmd, &spb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &vpt);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipe);
    for (const auto& inst : m_insts)
        drawInstance(cmd, m_pl, m_geoms[inst.geom], inst);
    vkCmdEndRenderPass(cmd);
}

void Renderer::hdrBarrier(VkCommandBuffer cmd, VkApp& app) {
    VkImageMemoryBarrier imb{};
    imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.image = app.hdrImage();
    imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &imb);
}

void Renderer::writeTonemapUbo(float ox, float oy, float sx, float sy) {
    const float tm[8] = {ox, oy, sx, sy, m_exposure, 0.0f, 0.0f, 0.0f};
    vkWriteBuffer(m_dev, m_tmMem, tm, 32);
}

void Renderer::writeLights() {
    LightsUbo u{};
    const int n = (int)std::min(m_lights.size(), (size_t)kMaxLights);
    u.count = n;
    for (int i = 0; i < n; ++i) {
        const Light& L = m_lights[i];
        LightGPU& g = u.lights[i];
        g.pos[0] = L.pos.x;
        g.pos[1] = L.pos.y;
        g.pos[2] = L.pos.z;
        g.intensity = L.p.intensity;
        g.color[0] = L.p.color.x;
        g.color[1] = L.p.color.y;
        g.color[2] = L.p.color.z;
        g.radius = L.p.radius;
        g.on = L.p.on;
        g.shadowNear = L.p.shadowNear;
        g.shadowFar = L.p.shadowFar;
        g.shadowNormalBias = L.p.shadowNormalBias;
        g.biasBase = L.p.shadowBiasBase;
        g.biasSlope = L.p.shadowBiasSlope;
        g.searchScale = L.p.shadowSearchScale;
        g.maxRadius = L.p.shadowMaxRadius;
    }
    vkWriteBuffer(m_dev, m_lightsMem, &u, sizeof(LightsUbo));
}

void Renderer::makeComputePass(const void* spv, uint32_t len, const std::vector<VkDescriptorSetLayoutBinding>& binds,
                               uint32_t pcSize, EnvPass& out) {
    vkMakeDslPool(m_dev, binds, 1, out.dsl, out.pool);
    out.pl = pcSize > 0 ? vkMakePipelineLayoutPC(m_dev, out.dsl, VK_SHADER_STAGE_COMPUTE_BIT, pcSize)
                        : vkMakePipelineLayout(m_dev, out.dsl);
    VkShaderModule cm = vkMakeModule(m_dev, spv, len);
    VkPipelineShaderStageCreateInfo cs{};
    cs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cs.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cs.module = cm;
    cs.pName = "main";
    VkComputePipelineCreateInfo cpc{};
    cpc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpc.stage = cs;
    cpc.layout = out.pl;
    VK(vkCreateComputePipelines(m_dev, VK_NULL_HANDLE, 1, &cpc, nullptr, &out.pipe));
    vkDestroyShaderModule(m_dev, cm, nullptr);
    VkDescriptorSetAllocateInfo sa{};
    sa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sa.descriptorPool = out.pool;
    sa.descriptorSetCount = 1;
    sa.pSetLayouts = &out.dsl;
    VK(vkAllocateDescriptorSets(m_dev, &sa, &out.set));
}

void Renderer::writeEnvSet(VkDescriptorSet set) {
    VkWriteDescriptorSet w[2]{};
    for (auto& entry : w) {
        entry.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        entry.dstSet = set;
        entry.descriptorCount = 1;
        entry.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    w[0].dstBinding = 9;
    w[0].pImageInfo = &m_envSpecInfo;
    w[1].dstBinding = 10;
    w[1].pImageInfo = &m_envIrrInfo;
    vkUpdateDescriptorSets(m_dev, 2, w, 0, nullptr);
}

void Renderer::setEnvironment(VkApp& app, const float* rgb, uint32_t w, uint32_t h) {
    if (m_envReady)
        return;
    const uint32_t cubeSize = 256;
    const uint32_t mips = 5;
    m_envCubeSize = cubeSize;
    m_envMips = mips;

    // equirect HDR: upload via staging buffer. Metal rejects linear tiling for 16/32-bit float
    // and does not support R32G32B32_SFLOAT at all, so repack RGB->RGBA and use
    // R32G32B32A32_SFLOAT in a device-local (optimal) image copied in on the GPU.
    const VkFormat eqFmt = VK_FORMAT_R32G32B32A32_SFLOAT;
    const size_t eqPixels = (size_t)w * h;
    std::vector<float> eqRgba(eqPixels * 4);
    for (size_t i = 0; i < eqPixels; ++i) {
        eqRgba[i * 4 + 0] = rgb[i * 3 + 0];
        eqRgba[i * 4 + 1] = rgb[i * 3 + 1];
        eqRgba[i * 4 + 2] = rgb[i * 3 + 2];
        eqRgba[i * 4 + 3] = 1.0f;
    }
    const VkDeviceSize eqBytes = eqPixels * 16;
    VkBuffer eqStg = VK_NULL_HANDLE;
    VkDeviceMemory eqStgMem = VK_NULL_HANDLE;
    vkMakeBuffer(m_dev, m_pdev, eqStg, eqStgMem, eqBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, eqRgba.data());

    VkImageCreateInfo eci{};
    eci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    eci.imageType = VK_IMAGE_TYPE_2D;
    eci.format = eqFmt;
    eci.extent = {w, h, 1};
    eci.mipLevels = 1;
    eci.arrayLayers = 1;
    eci.samples = VK_SAMPLE_COUNT_1_BIT;
    eci.tiling = VK_IMAGE_TILING_OPTIMAL;
    eci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    eci.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    VK(vkCreateImage(m_dev, &eci, nullptr, &m_envEq.img));
    VkMemoryRequirements emr;
    vkGetImageMemoryRequirements(m_dev, m_envEq.img, &emr);
    VkMemoryAllocateInfo emaa{};
    emaa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    emaa.allocationSize = emr.size;
    emaa.memoryTypeIndex = vkFindMemoryType(m_pdev, emr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(m_dev, &emaa, nullptr, &m_envEq.mem));
    VK(vkBindImageMemory(m_dev, m_envEq.img, m_envEq.mem, 0));
    VkImageViewCreateInfo evci{};
    evci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    evci.image = m_envEq.img;
    evci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    evci.format = eqFmt;
    evci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(m_dev, &evci, nullptr, &m_envEq.view));

    vkMakeCubeImage(m_dev, m_pdev, cubeSize, mips, m_envPre.img, m_envPre.mem, m_envPre.view);
    vkMakeCubeImage(m_dev, m_pdev, cubeSize, 1, m_envIrr.img, m_envIrr.mem, m_envIrr.view);
    m_envPreMips.resize(mips);
    for (uint32_t m = 0; m < mips; ++m)
        vkMakeCubeMipArrayView(m_dev, m_envPre.img, m, m_envPreMips[m]);
    vkMakeCubeMipArrayView(m_dev, m_envIrr.img, 0, m_envIrrArr);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_pdev, &props);
    const float maxAniso = props.limits.maxSamplerAnisotropy;
    vkMakeSamplerEx(m_dev, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 0.0f, 1.0f,
                    m_eqSampler);
    vkMakeSamplerEx(m_dev, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, true,
                    (float)(mips - 1), maxAniso, m_cubeSampler);

    m_envSpecInfo = {m_cubeSampler, m_envPre.view, VK_IMAGE_LAYOUT_GENERAL};
    m_envIrrInfo = {m_cubeSampler, m_envIrr.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo eqInfo{m_eqSampler, m_envEq.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo preSam{m_cubeSampler, m_envPre.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo preImg{VK_NULL_HANDLE, m_envPreMips[0], VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo irrImg{VK_NULL_HANDLE, m_envIrrArr, VK_IMAGE_LAYOUT_GENERAL};

    const std::vector<VkDescriptorSetLayoutBinding> cubeBinds = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
    };
    makeComputePass(env_cube_spv, env_cube_spv_len / 4, cubeBinds, 4, m_pcCube);
    makeComputePass(env_prefilter_spv, env_prefilter_spv_len / 4, cubeBinds, 8, m_pcPref);
    makeComputePass(env_irradiance_spv, env_irradiance_spv_len / 4, cubeBinds, 4, m_pcIrr);

    auto writeImg = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo& ii) {
        VkWriteDescriptorSet wr{};
        wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet = set;
        wr.dstBinding = binding;
        wr.descriptorCount = 1;
        wr.descriptorType = type;
        wr.pImageInfo = &ii;
        vkUpdateDescriptorSets(m_dev, 1, &wr, 0, nullptr);
    };
    writeImg(m_pcCube.set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eqInfo);
    writeImg(m_pcCube.set, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, preImg);
    writeImg(m_pcPref.set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, preSam);
    writeImg(m_pcPref.set, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, preImg);
    writeImg(m_pcIrr.set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, preSam);
    writeImg(m_pcIrr.set, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, irrImg);

    VkCommandBuffer cmd = app.beginCommands();

    VkBufferImageCopy bic{};
    bic.bufferOffset = 0;
    bic.bufferRowLength = w;
    bic.bufferImageHeight = h;
    bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    bic.imageOffset = {0, 0, 0};
    bic.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, eqStg, m_envEq.img, VK_IMAGE_LAYOUT_GENERAL, 1, &bic);
    VkImageMemoryBarrier eqBar{};
    eqBar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    eqBar.image = m_envEq.img;
    eqBar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    eqBar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    eqBar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    eqBar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    eqBar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    eqBar.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    eqBar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &eqBar);

    auto imgBar = [&](VkImage img, uint32_t lvl) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.image = img;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, lvl, 0, 6};
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
    };
    const uint32_t groups = (cubeSize + 7) / 8;
    const int pcSize = (int)cubeSize;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcCube.pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcCube.pl, 0, 1, &m_pcCube.set, 0, nullptr);
    vkCmdPushConstants(cmd, m_pcCube.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &pcSize);
    vkCmdDispatch(cmd, groups, groups, 6);
    imgBar(m_envPre.img, mips);

    for (uint32_t mip = 1; mip < mips; ++mip) {
        const uint32_t size = cubeSize >> mip;
        const VkDescriptorImageInfo mi{VK_NULL_HANDLE, m_envPreMips[mip], VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet wr{};
        wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet = m_pcPref.set;
        wr.dstBinding = 1;
        wr.descriptorCount = 1;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        wr.pImageInfo = &mi;
        vkUpdateDescriptorSets(m_dev, 1, &wr, 0, nullptr);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcPref.pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcPref.pl, 0, 1, &m_pcPref.set, 0, nullptr);
        const int pcPref[2] = {(int)mip, (int)size};
        vkCmdPushConstants(cmd, m_pcPref.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pcPref);
        vkCmdDispatch(cmd, (size + 7) / 8, (size + 7) / 8, 6);
        imgBar(m_envPre.img, mips);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcIrr.pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcIrr.pl, 0, 1, &m_pcIrr.set, 0, nullptr);
    vkCmdPushConstants(cmd, m_pcIrr.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &pcSize);
    vkCmdDispatch(cmd, groups, groups, 6);
    imgBar(m_envIrr.img, 1);
    app.submit(cmd);
    vkFreeBuffer(m_dev, eqStg, eqStgMem);

    m_envReady = true;
    for (auto& inst : m_insts)
        writeEnvSet(inst.set);
}

void Renderer::initShadow(VkApp& app) {
    const uint32_t nLayers = kMaxLights * 6;
    vkMakeCubeArrayImage(m_dev, m_pdev, m_shadowSize, nLayers, VK_FORMAT_R32_SFLOAT, m_shadowCube, m_shadowCubeMem);
    vkMakeDepthCubeImage(m_dev, m_pdev, m_shadowSize, m_shadowDepth, m_shadowDepthMem);
    for (uint32_t l = 0; l < nLayers; ++l)
        vkMakeCubeFaceView(m_dev, m_shadowCube, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, l,
                           m_shadowColorFace[l]);
    for (uint32_t f = 0; f < 6; ++f)
        vkMakeCubeFaceView(m_dev, m_shadowDepth, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, f,
                           m_shadowDepthFace[f]);
    vkMakeCubeArrayView(m_dev, m_shadowCube, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 0, nLayers,
                        m_shadowSampleView);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_pdev, &props);
    vkMakeSamplerEx(m_dev, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 0.0f,
                    1.0f, m_shadowSampler);
    m_shadowInfo = {m_shadowSampler, m_shadowSampleView, VK_IMAGE_LAYOUT_GENERAL};

    VkAttachmentDescription att[2]{};
    att[0].format = VK_FORMAT_R32_SFLOAT;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    att[1].format = VK_FORMAT_D32_SFLOAT;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                    {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription sub{};
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref[0];
    sub.pDepthStencilAttachment = &ref[1];
    VkRenderPassCreateInfo rpc{};
    rpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpc.attachmentCount = 2;
    rpc.pAttachments = att;
    rpc.subpassCount = 1;
    rpc.pSubpasses = &sub;
    VK(vkCreateRenderPass(m_dev, &rpc, nullptr, &m_shadowRp));

    for (uint32_t i = 0; i < kMaxLights; ++i)
        for (uint32_t f = 0; f < 6; ++f) {
            VkImageView atts[2] = {m_shadowColorFace[i * 6 + f], m_shadowDepthFace[f]};
            VkFramebufferCreateInfo fbi{};
            fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbi.renderPass = m_shadowRp;
            fbi.attachmentCount = 2;
            fbi.pAttachments = atts;
            fbi.width = m_shadowSize;
            fbi.height = m_shadowSize;
            fbi.layers = 1;
            VK(vkCreateFramebuffer(m_dev, &fbi, nullptr, &m_shadowFb[i * 6 + f]));
        }

    m_shadowPl = vkMakePipelineLayoutPC(m_dev, m_dsl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 80);
    m_shadowPipe = makeGraphicsPipeline(m_dev, m_shadowRp, shadow_vert_spv, shadow_vert_spv_len / 4, shadow_frag_spv,
                                        shadow_frag_spv_len / 4, m_shadowPl, true, false);

    VkCommandBuffer cmd = app.beginCommands();
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, nLayers};
    const float farD = m_shadowRange + 1.0f;
    VkClearColorValue clear{{farD, farD, farD, 1.0f}};
    vkCmdClearColorImage(cmd, m_shadowCube, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
    app.submit(cmd);
}

void Renderer::renderShadowCubes(VkCommandBuffer cmd) {
    const uint32_t n = (uint32_t)std::min(m_lights.size(), (size_t)kMaxLights);
    if (n == 0)
        return;
    struct Face {
        V3 right, up, back;
    };
    const Face faces[6] = {
        {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}},   {{0, 0, -1}, {0, 1, 0}, {-1, 0, 0}}, {{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}}, {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}},
    };
    struct Pc {
        float viewProj[16];
        float lightPosBias[4];
    };
    VkViewport vp{0.0f, 0.0f, (float)m_shadowSize, (float)m_shadowSize, 0.0f, 1.0f};
    VkRect2D sc{0, 0, m_shadowSize, m_shadowSize};

    for (uint32_t i = 0; i < n; ++i) {
        const Light& L = m_lights[i];
        if (L.p.on < 0.5f)
            continue;
        const float shadowNear = std::max(L.p.shadowNear, 0.001f);
        const float shadowFar = std::max(L.p.shadowFar, shadowNear + 0.01f);
        const Mat4 proj = perspective(90.0f, 1.0f, shadowNear, shadowFar);
        const float farD = shadowFar + 1.0f;

        for (uint32_t f = 0; f < 6; ++f) {
            const Face& fc = faces[f];
            Mat4 view{};
            view.m[0] = fc.right.x;
            view.m[1] = fc.up.x;
            view.m[2] = fc.back.x;
            view.m[4] = fc.right.y;
            view.m[5] = fc.up.y;
            view.m[6] = fc.back.y;
            view.m[8] = fc.right.z;
            view.m[9] = fc.up.z;
            view.m[10] = fc.back.z;
            view.m[12] = -vDot(fc.right, L.pos);
            view.m[13] = -vDot(fc.up, L.pos);
            view.m[14] = -vDot(fc.back, L.pos);
            view.m[15] = 1.0f;
            const Mat4 vpLight = mul4(proj, view);

            Pc pc{};
            std::memcpy(pc.viewProj, vpLight.m, 64);
            pc.lightPosBias[0] = L.pos.x;
            pc.lightPosBias[1] = L.pos.y;
            pc.lightPosBias[2] = L.pos.z;
            pc.lightPosBias[3] = L.p.shadowNormalBias;

            VkClearValue cv[2]{};
            cv[0].color = {farD, 0.0f, 0.0f, 1.0f};
            cv[1].depthStencil = {1.0f, 0};
            VkRenderPassBeginInfo rpb{};
            rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpb.renderPass = m_shadowRp;
            rpb.framebuffer = m_shadowFb[i * 6 + f];
            rpb.renderArea = {{0, 0}, {m_shadowSize, m_shadowSize}};
            rpb.clearValueCount = 2;
            rpb.pClearValues = cv;
            vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipe);
            vkCmdPushConstants(cmd, m_shadowPl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(Pc), &pc);
            for (const auto& inst : m_insts)
                drawInstance(cmd, m_shadowPl, m_geoms[inst.geom], inst);
            vkCmdEndRenderPass(cmd);
        }
    }

    VkImageMemoryBarrier imb{};
    imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imb.image = m_shadowCube;
    imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, n * 6};
    imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imb.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &imb);
}

void Renderer::drawInstance(VkCommandBuffer cmd, VkPipelineLayout pl, const GpuMesh& g, const InstancedMesh& inst) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &inst.set, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, g.ibuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, g.idxCount, 1, 0, 0, 0);
}

void Renderer::rebuildTonemapSet(VkApp& app) {
    vkResetDescriptorPool(m_dev, m_tmPool, 0);
    VkDescriptorSet sets[2]{};
    const uint32_t n = m_xrReady ? 2 : 1;
    VkDescriptorSetLayout layouts[2] = {m_tmDsl, m_tmDsl}; // pSetLayouts must have n entries
    VkDescriptorSetAllocateInfo sai{};
    sai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sai.descriptorPool = m_tmPool;
    sai.descriptorSetCount = n;
    sai.pSetLayouts = layouts;
    VK(vkAllocateDescriptorSets(m_dev, &sai, sets));
    m_tmSet = sets[0];
    m_xrSet = m_xrReady ? sets[1] : VK_NULL_HANDLE;
    VkDescriptorImageInfo ii;
    ii.sampler = app.hdrSampler();
    ii.imageView = app.hdrView();
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo bi{m_tmUbuf, 0, 32};
    VkDescriptorBufferInfo xbi{m_xrTmUbuf, 0, 32};
    VkWriteDescriptorSet w[4]{};
    for (int s = 0; s < 2; ++s) {
        w[2 * s].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[2 * s].dstSet = sets[s];
        w[2 * s].dstBinding = 0;
        w[2 * s].descriptorCount = 1;
        w[2 * s].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[2 * s].pImageInfo = &ii;
        w[2 * s + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[2 * s + 1].dstSet = sets[s];
        w[2 * s + 1].dstBinding = 1;
        w[2 * s + 1].descriptorCount = 1;
        w[2 * s + 1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[2 * s + 1].pBufferInfo = &bi;
    }
    if (m_xrReady)
        w[3].pBufferInfo = &xbi;
    vkUpdateDescriptorSets(m_dev, 2 * n, w, 0, nullptr);
    m_tmSrcView = app.hdrView();
}

void Renderer::draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui,
                    float exposure) {
    m_exposure = exposure;
    writeViewUbo(m_viewProj, m_camPos);
    prepBuffers();
    const VkExtent2D ext = app.extent();
    renderShadowCubes(cmd);
    renderScenePass(cmd, app, bg);
    hdrBarrier(cmd, app);
    if (app.hdrView() != m_tmSrcView)
        rebuildTonemapSet(app);
    writeTonemapUbo(0.0f, 0.0f, (float)ext.width, (float)ext.height);
    VkViewport vpt{0, (float)ext.height, (float)ext.width, -(float)ext.height, 0, 1};
    VkRect2D sc{0, 0, ext.width, ext.height};
    VkClearValue bcv[1]{};
    bcv[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    VkRenderPassBeginInfo bpb{};
    bpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bpb.renderPass = app.renderPass();
    bpb.framebuffer = app.frameBuffer(fb);
    bpb.renderArea = {{0, 0}, ext};
    bpb.clearValueCount = 1;
    bpb.pClearValues = bcv;
    vkCmdBeginRenderPass(cmd, &bpb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &vpt);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tmPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tmPl, 0, 1, &m_tmSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    if (imgui && imgui->CmdLists.Size > 0)
        ImGui_ImplVulkan_RenderDrawData(imgui, cmd);
    vkCmdEndRenderPass(cmd);
    app.submit(cmd);
}

Mat4 Renderer::eyeVP(int eye) const {
    const XrEyeData& e = m_vrEyes[eye];
    const Mat4 proj = projFov(e.tanL, e.tanR, e.tanU, e.tanD, 0.1f, 300.0f);
    return mul4(proj, viewFromPose(e.pos, e.quat));
}

// letterbox the eye image (its own aspect) into the eye's half of the window
VkRect2D Renderer::eyeRegion(int eye, VkExtent2D ext) const {
    const XrEyeData& e = m_vrEyes[eye];
    const float aspect = (e.tanR - e.tanL) / (e.tanU - e.tanD);
    const uint32_t halfW = ext.width / 2;
    float rw, rh;
    if (aspect > (float)halfW / (float)ext.height) {
        rw = (float)halfW;
        rh = rw / aspect;
    } else {
        rh = (float)ext.height;
        rw = rh * aspect;
    }
    const float ox = eye * (float)halfW + ((float)halfW - rw) * 0.5f;
    const float oy = ((float)ext.height - rh) * 0.5f;
    return {(int)ox, (int)oy, (uint32_t)rw, (uint32_t)rh};
}

void Renderer::setVrEyes(const XrEyeData* eyes, int nEyes) {
    m_vrEyesN = std::clamp(nEyes, 1, 2);
    for (int i = 0; i < m_vrEyesN; ++i)
        m_vrEyes[i] = eyes[i];
}

void Renderer::ensureNoClearFbs(VkApp& app) {
    const VkExtent2D ext = app.extent();
    if (m_noClearFbs.size() == app.imageCount() && m_noClearExt.width == ext.width && m_noClearExt.height == ext.height)
        return;
    for (auto* fb : m_noClearFbs)
        vkDestroyFramebuffer(m_dev, fb, nullptr);
    m_noClearFbs.clear();
    m_noClearFbs.resize(app.imageCount());
    for (uint32_t i = 0; i < app.imageCount(); ++i) {
        const VkImageView att = app.view(i);
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = m_noClearRp;
        fci.attachmentCount = 1;
        fci.pAttachments = &att;
        fci.width = ext.width;
        fci.height = ext.height;
        fci.layers = 1;
        VK(vkCreateFramebuffer(m_dev, &fci, nullptr, &m_noClearFbs[i]));
    }
    m_noClearExt = ext;
}

void Renderer::drawVr(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui,
                      float exposure, const uint32_t* xrImg, int nXr) {
    m_exposure = exposure;
    prepBuffers();
    const VkExtent2D ext = app.extent();
    renderShadowCubes(cmd);
    ensureNoClearFbs(app);
    VkViewport vpt{0, (float)ext.height, (float)ext.width, -(float)ext.height, 0, 1};
    VkRect2D full{0, 0, ext.width, ext.height};
    VkClearValue bcv[1]{};
    bcv[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int eye = 0; eye < m_vrEyesN; ++eye) {
        // the view + tonemap UBOs are single-slot host buffers, so each eye is
        // its own fence-waited submit to avoid the next eye clobbering this one
        writeViewUbo(eyeVP(eye), m_vrEyes[eye].pos);
        renderScenePass(cmd, app, bg);
        hdrBarrier(cmd, app);
        if (app.hdrView() != m_tmSrcView)
            rebuildTonemapSet(app);
        const VkRect2D r = eyeRegion(eye, ext);
        writeTonemapUbo((float)r.offset.x, (float)r.offset.y, (float)r.extent.width, (float)r.extent.height);
        const bool first = eye == 0;
        const bool last = eye == m_vrEyesN - 1;
        VkRenderPassBeginInfo bpb{};
        bpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        bpb.renderPass = first ? app.renderPass() : m_noClearRp;
        bpb.framebuffer = first ? app.frameBuffer(fb) : m_noClearFbs[fb];
        bpb.renderArea = {{0, 0}, ext};
        bpb.clearValueCount = 1;
        bpb.pClearValues = bcv;
        vkCmdBeginRenderPass(cmd, &bpb, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(cmd, 0, 1, &vpt);
        vkCmdSetScissor(cmd, 0, 1, &r);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tmPipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tmPl, 0, 1, &m_tmSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        if (last && imgui && imgui->CmdLists.Size > 0) {
            vkCmdSetScissor(cmd, 0, 1, &full);
            ImGui_ImplVulkan_RenderDrawData(imgui, cmd);
        }
        vkCmdEndRenderPass(cmd);
        if (xrImg && eye < nXr)
            drawXrEye(cmd, app, eye, xrImg[eye]);
        app.submit(cmd);
        if (!last)
            cmd = app.beginCommands();
    }
}

void Renderer::initXrTarget(VkApp& app, VkFormat fmt, int nEyes, const std::vector<std::vector<VkImage>>& images,
                            const VkExtent2D* exts) {
    m_xrFmt = fmt;
    VkAttachmentDescription att{};
    att.format = fmt;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpc{};
    rpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpc.attachmentCount = 1;
    rpc.pAttachments = &att;
    rpc.subpassCount = 1;
    rpc.pSubpasses = &sub;
    VK(vkCreateRenderPass(m_dev, &rpc, nullptr, &m_xrRp));
    m_xrPipe = makeGraphicsPipeline(m_dev, m_xrRp, tonemap_vert_spv, tonemap_vert_spv_len / 4, tonemap_frag_spv,
                                    tonemap_frag_spv_len / 4, m_tmPl, false);
    vkMakeBuffer(m_dev, m_pdev, m_xrTmUbuf, m_xrTmMem, 32, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    m_xrViews.resize(nEyes);
    m_xrFbs.resize(nEyes);
    for (int i = 0; i < nEyes; ++i) {
        m_xrExt[i] = exts[i];
        m_xrViews[i].resize(images[i].size());
        m_xrFbs[i].resize(images[i].size());
        for (size_t k = 0; k < images[i].size(); ++k) {
            VkImageViewCreateInfo vci{};
            vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vci.image = images[i][k];
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = fmt;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK(vkCreateImageView(m_dev, &vci, nullptr, &m_xrViews[i][k]));
            VkFramebufferCreateInfo fci{};
            fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fci.renderPass = m_xrRp;
            fci.attachmentCount = 1;
            fci.pAttachments = &m_xrViews[i][k];
            fci.width = exts[i].width;
            fci.height = exts[i].height;
            fci.layers = 1;
            VK(vkCreateFramebuffer(m_dev, &fci, nullptr, &m_xrFbs[i][k]));
        }
    }
    m_xrReady = true;
    rebuildTonemapSet(app);
}

void Renderer::drawXrEye(VkCommandBuffer cmd, VkApp& app, int eye, uint32_t imgIdx) {
    if (!m_xrReady)
        return;
    if (app.hdrView() != m_tmSrcView)
        rebuildTonemapSet(app);
    const uint32_t w = m_xrExt[eye].width, h = m_xrExt[eye].height;
    const float tm[8] = {0.0f, 0.0f, (float)w, (float)h, m_exposure, 0.0f, 0.0f, 0.0f};
    vkWriteBuffer(m_dev, m_xrTmMem, tm, 32);
    VkClearValue cv[1]{};
    cv[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    VkRenderPassBeginInfo rpb{};
    rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpb.renderPass = m_xrRp;
    rpb.framebuffer = m_xrFbs[eye][imgIdx];
    rpb.renderArea = {{0, 0}, {w, h}};
    rpb.clearValueCount = 1;
    rpb.pClearValues = cv;
    vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, (float)h, (float)w, -(float)h, 0, 1};
    VkRect2D sc{0, 0, w, h};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_xrPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tmPl, 0, 1, &m_xrSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void Renderer::shutdown() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    for (auto& inst : m_insts)
        vkFreeBuffer(m_dev, inst.modelUbuf, inst.modelMem);
    for (auto& g : m_geoms) {
        vkFreeBuffer(m_dev, g.pos, g.posMem);
        vkFreeBuffer(m_dev, g.nrm, g.nrmMem);
        vkFreeBuffer(m_dev, g.uv, g.uvMem);
        vkFreeBuffer(m_dev, g.ibuf, g.ibmem);
    }
    for (auto& t : m_texs)
        vkFreeImage2D(m_dev, t.img, t.mem, t.view);
    vkFreeImage2D(m_dev, m_white.img, m_white.mem, m_white.view);
    if (m_texSampler)
        vkDestroySampler(m_dev, m_texSampler, nullptr);
    if (m_envReady) {
        for (const auto& p : {m_pcCube, m_pcPref, m_pcIrr}) {
            if (p.pool)
                vkDestroyDescriptorPool(m_dev, p.pool, nullptr);
            if (p.dsl)
                vkDestroyDescriptorSetLayout(m_dev, p.dsl, nullptr);
            if (p.pl)
                vkDestroyPipelineLayout(m_dev, p.pl, nullptr);
            if (p.pipe)
                vkDestroyPipeline(m_dev, p.pipe, nullptr);
        }
        vkFreeImage2D(m_dev, m_envEq.img, m_envEq.mem, m_envEq.view);
        vkFreeImage2D(m_dev, m_envPre.img, m_envPre.mem, m_envPre.view);
        vkFreeImage2D(m_dev, m_envIrr.img, m_envIrr.mem, m_envIrr.view);
        for (VkImageView v : m_envPreMips)
            vkDestroyImageView(m_dev, v, nullptr);
        if (m_envIrrArr)
            vkDestroyImageView(m_dev, m_envIrrArr, nullptr);
        if (m_eqSampler)
            vkDestroySampler(m_dev, m_eqSampler, nullptr);
        if (m_cubeSampler)
            vkDestroySampler(m_dev, m_cubeSampler, nullptr);
    }
    if (m_pool)
        vkDestroyDescriptorPool(m_dev, m_pool, nullptr);
    if (m_dsl)
        vkDestroyDescriptorSetLayout(m_dev, m_dsl, nullptr);
    if (m_pl)
        vkDestroyPipelineLayout(m_dev, m_pl, nullptr);
    if (m_pipe)
        vkDestroyPipeline(m_dev, m_pipe, nullptr);
    for (auto* fb : m_noClearFbs)
        vkDestroyFramebuffer(m_dev, fb, nullptr);
    if (m_noClearRp)
        vkDestroyRenderPass(m_dev, m_noClearRp, nullptr);
    if (m_xrReady) {
        for (auto& eye : m_xrFbs)
            for (auto* fb : eye)
                vkDestroyFramebuffer(m_dev, fb, nullptr);
        for (auto& eye : m_xrViews)
            for (auto* v : eye)
                vkDestroyImageView(m_dev, v, nullptr);
        if (m_xrPipe)
            vkDestroyPipeline(m_dev, m_xrPipe, nullptr);
        if (m_xrRp)
            vkDestroyRenderPass(m_dev, m_xrRp, nullptr);
        vkFreeBuffer(m_dev, m_xrTmUbuf, m_xrTmMem);
    }
    if (m_tmPool)
        vkDestroyDescriptorPool(m_dev, m_tmPool, nullptr);
    if (m_tmDsl)
        vkDestroyDescriptorSetLayout(m_dev, m_tmDsl, nullptr);
    if (m_tmPl)
        vkDestroyPipelineLayout(m_dev, m_tmPl, nullptr);
    if (m_tmPipe)
        vkDestroyPipeline(m_dev, m_tmPipe, nullptr);
    vkFreeBuffer(m_dev, m_tmUbuf, m_tmMem);
    vkFreeBuffer(m_dev, m_vpUbuf, m_vpMem);
    for (uint32_t l = 0; l < kMaxLights * 6; ++l) {
        vkDestroyFramebuffer(m_dev, m_shadowFb[l], nullptr);
        vkDestroyImageView(m_dev, m_shadowColorFace[l], nullptr);
    }
    for (uint32_t f = 0; f < 6; ++f)
        vkDestroyImageView(m_dev, m_shadowDepthFace[f], nullptr);
    vkFreeBuffer(m_dev, m_lightsUbuf, m_lightsMem);
    if (m_shadowPipe)
        vkDestroyPipeline(m_dev, m_shadowPipe, nullptr);
    if (m_shadowPl)
        vkDestroyPipelineLayout(m_dev, m_shadowPl, nullptr);
    if (m_shadowRp)
        vkDestroyRenderPass(m_dev, m_shadowRp, nullptr);
    if (m_shadowSampler)
        vkDestroySampler(m_dev, m_shadowSampler, nullptr);
    if (m_shadowSampleView)
        vkDestroyImageView(m_dev, m_shadowSampleView, nullptr);
    if (m_shadowCube) {
        vkDestroyImage(m_dev, m_shadowCube, nullptr);
        vkFreeMemory(m_dev, m_shadowCubeMem, nullptr);
    }
    if (m_shadowDepth) {
        vkDestroyImage(m_dev, m_shadowDepth, nullptr);
        vkFreeMemory(m_dev, m_shadowDepthMem, nullptr);
    }
}
