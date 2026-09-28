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
    float pad0;
    float lightPos[3];
    float lightIntensity;
    float lightColor[3];
    float lightRadius;
    float lightOn;
    float envIntensity;
    float shadowNear;
    float shadowFar;
    float shadowNormalBias;
    float shadowBiasBase;
    float shadowBiasSlope;
    float shadowSearchScale;
    float shadowMaxRadius;
    float pad[3];
};
static_assert(sizeof(ViewUbo) == 160, "ViewUbo must match the shader std430 layout");

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
    const auto binds = meshBinds();
    vkMakeDslPool(m_dev, binds, (uint32_t)nInstances, m_dsl, m_pool);
    m_pl = vkMakePipelineLayout(m_dev, m_dsl);
    m_pipe = makeGraphicsPipeline(m_dev, app.sceneRenderPass(), mesh_vert_spv, mesh_vert_spv_len / 4, mesh_frag_spv,
                                  mesh_frag_spv_len / 4, m_pl, true);

    const uint8_t white[4] = {255, 255, 255, 255};
    vkMakeImage2D(m_dev, m_pdev, 1, 1, white, m_white.img, m_white.mem, m_white.view);
    vkMakeSampler(m_dev, VK_SAMPLER_ADDRESS_MODE_REPEAT, m_texSampler);

    vkMakeBuffer(m_dev, m_pdev, m_tmUbuf, m_tmMem, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    const auto tmBinds = tonemapBinds();
    vkMakeDslPool(m_dev, tmBinds, 2, m_tmDsl, m_tmPool);
    m_tmPl = vkMakePipelineLayout(m_dev, m_tmDsl);
    m_tmPipe = makeGraphicsPipeline(m_dev, app.renderPass(), tonemap_vert_spv, tonemap_vert_spv_len / 4,
                                   tonemap_frag_spv, tonemap_frag_spv_len / 4, m_tmPl, false);
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
    VkWriteDescriptorSet sw{};
    sw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sw.dstSet = inst.set;
    sw.dstBinding = 12;
    sw.descriptorCount = 1;
    sw.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sw.pImageInfo = &m_shadowInfo;
    vkUpdateDescriptorSets(m_dev, 1, &sw, 0, nullptr);
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

void Renderer::writeViewUbo() {
    ViewUbo u{};
    std::memcpy(u.viewProj, m_viewProj.m, 64);
    u.camPos[0] = m_camPos.x;
    u.camPos[1] = m_camPos.y;
    u.camPos[2] = m_camPos.z;
    if (!m_lights.empty()) {
        const Light& L = m_lights.front();
        u.lightPos[0] = L.pos.x;
        u.lightPos[1] = L.pos.y;
        u.lightPos[2] = L.pos.z;
        u.lightIntensity = L.p.intensity;
        u.lightColor[0] = L.p.color.x;
        u.lightColor[1] = L.p.color.y;
        u.lightColor[2] = L.p.color.z;
        u.lightRadius = L.p.radius;
        u.lightOn = L.p.on;
        u.shadowNear = L.p.shadowNear;
        u.shadowFar = L.p.shadowFar;
        u.shadowNormalBias = L.p.shadowNormalBias;
        u.shadowBiasBase = L.p.shadowBiasBase;
        u.shadowBiasSlope = L.p.shadowBiasSlope;
        u.shadowSearchScale = L.p.shadowSearchScale;
        u.shadowMaxRadius = L.p.shadowMaxRadius;
    }
    u.envIntensity = m_envIntensity;
    vkWriteBuffer(m_dev, m_vpMem, &u, sizeof(ViewUbo));
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

    vkMakeImage2DF32(m_dev, m_pdev, w, h, rgb, m_envEq.img, m_envEq.mem, m_envEq.view);
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

    m_envReady = true;
    for (auto& inst : m_insts)
        writeEnvSet(inst.set);
}

void Renderer::initShadow(VkApp& app) {
    vkMakeCubeImage(m_dev, m_pdev, m_shadowSize, 1, m_shadowCube, m_shadowCubeMem, m_shadowCubeView, VK_FORMAT_R32_SFLOAT);
    vkMakeDepthCubeImage(m_dev, m_pdev, m_shadowSize, m_shadowDepth, m_shadowDepthMem);
    for (uint32_t f = 0; f < 6; ++f) {
        vkMakeCubeFaceView(m_dev, m_shadowCube, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, f, m_shadowColorFace[f]);
        vkMakeCubeFaceView(m_dev, m_shadowDepth, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, f, m_shadowDepthFace[f]);
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_pdev, &props);
    vkMakeSamplerEx(m_dev, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 0.0f, 1.0f,
                    m_shadowSampler);
    m_shadowInfo = {m_shadowSampler, m_shadowCubeView, VK_IMAGE_LAYOUT_GENERAL};

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

    for (uint32_t f = 0; f < 6; ++f) {
        VkImageView atts[2] = {m_shadowColorFace[f], m_shadowDepthFace[f]};
        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = m_shadowRp;
        fbi.attachmentCount = 2;
        fbi.pAttachments = atts;
        fbi.width = m_shadowSize;
        fbi.height = m_shadowSize;
        fbi.layers = 1;
        VK(vkCreateFramebuffer(m_dev, &fbi, nullptr, &m_shadowFb[f]));
    }

    m_shadowPl = vkMakePipelineLayoutPC(m_dev, m_dsl, VK_SHADER_STAGE_VERTEX_BIT, 64);
    m_shadowPipe = makeGraphicsPipeline(m_dev, m_shadowRp, shadow_vert_spv, shadow_vert_spv_len / 4, shadow_frag_spv,
                                        shadow_frag_spv_len / 4, m_shadowPl, true, false);

    VkCommandBuffer cmd = app.beginCommands();
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
    const float farD = m_shadowRange + 1.0f;
    VkClearColorValue clear{{farD, farD, farD, 1.0f}};
    vkCmdClearColorImage(cmd, m_shadowCube, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
    app.submit(cmd);
}

void Renderer::renderShadowCube(VkCommandBuffer cmd) {
    if (m_lights.empty() || m_lights.front().p.on < 0.5f)
        return;
    const Light& L = m_lights.front();
    struct Face {
        V3 right, up, back;
    };
    const Face faces[6] = {
        {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}},
        {{0, 0, -1}, {0, 1, 0}, {-1, 0, 0}},
        {{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}},
        {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}},
    };
    const float shadowNear = std::max(L.p.shadowNear, 0.001f);
    const float shadowFar = std::max(L.p.shadowFar, shadowNear + 0.01f);
    const Mat4 proj = perspective(90.0f, 1.0f, shadowNear, shadowFar);
    VkViewport vp{0.0f, (float)m_shadowSize, (float)m_shadowSize, -(float)m_shadowSize, 0.0f, 1.0f};
    VkRect2D sc{0, 0, m_shadowSize, m_shadowSize};
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

        VkClearValue cv[2]{};
        cv[0].color = {farD, 0.0f, 0.0f, 1.0f};
        cv[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rpb{};
        rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpb.renderPass = m_shadowRp;
        rpb.framebuffer = m_shadowFb[f];
        rpb.renderArea = {{0, 0}, {m_shadowSize, m_shadowSize}};
        rpb.clearValueCount = 2;
        rpb.pClearValues = cv;
        vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipe);
        vkCmdPushConstants(cmd, m_shadowPl, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, vpLight.m);
        for (const auto& inst : m_insts)
            drawInstance(cmd, m_shadowPl, m_geoms[inst.geom], inst);
        vkCmdEndRenderPass(cmd);
    }

    VkImageMemoryBarrier imb{};
    imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imb.image = m_shadowCube;
    imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
    imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imb.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                          nullptr, 0, nullptr, 1, &imb);
}

static void writePgm(const char* path, uint32_t w, uint32_t h, const float* dist, float far) {
    std::vector<unsigned char> px((size_t)w * h);
    float mn = 1e30f, mx = -1e30f;
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        mn = std::min(mn, dist[i]);
        mx = std::max(mx, dist[i]);
        const float t = std::clamp(dist[i] / far, 0.0f, 1.0f);
        px[i] = (unsigned char)(255.0f * (1.0f - t)); // near light -> bright, background -> dark
    }
    FILE* fp = std::fopen(path, "wb");
    if (!fp) {
        std::fprintf(stderr, "shadow dump: cannot open %s\n", path);
        return;
    }
    std::fprintf(fp, "P5\n%u %u\n255\n", w, h);
    std::fwrite(px.data(), 1, px.size(), fp);
    std::fclose(fp);
    std::printf("  %s  dist[min=%.2f max=%.2f]\n", path, mn, mx);
}

void Renderer::dumpShadowMap(VkApp& app, const char* prefix) {
    const uint32_t S = m_shadowSize;
    const uint32_t faceBytes = S * S * 4;
    const VkDeviceSize totalBytes = (VkDeviceSize)faceBytes * 6;
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    vkMakeBuffer(m_dev, m_pdev, staging, stagingMem, totalBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr);

    VkCommandBuffer cmd = app.beginCommands();
    VkBufferImageCopy region{};
    region.bufferRowLength = 0;
    region.bufferImageHeight = S;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {S, S, 1};
    for (uint32_t f = 0; f < 6; ++f) {
        region.bufferOffset = (VkDeviceSize)f * faceBytes;
        region.imageSubresource.baseArrayLayer = f;
        vkCmdCopyImageToBuffer(cmd, m_shadowCube, VK_IMAGE_LAYOUT_GENERAL, staging, 1, &region);
    }
    app.submit(cmd);

    float* data;
    VK(vkMapMemory(m_dev, stagingMem, 0, totalBytes, 0, (void**)&data));
    const float lightFar = m_lights.empty() ? 0.0f : m_lights.front().p.shadowFar;
    const float far = lightFar > 0.01f ? lightFar : m_shadowRange;
    char path[512];
    for (uint32_t f = 0; f < 6; ++f) {
        std::snprintf(path, sizeof(path), "%s_face%u.pgm", prefix, f);
        writePgm(path, S, S, data + (size_t)f * S * S, far);
    }
    vkUnmapMemory(m_dev, stagingMem);
    vkFreeBuffer(m_dev, staging, stagingMem);
    std::printf("shadow map dumped -> %s_face[0..5].pgm (far=%.1f)\n", prefix, far);
}

void Renderer::drawInstance(VkCommandBuffer cmd, VkPipelineLayout pl, const GpuMesh& g, const InstancedMesh& inst) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &inst.set, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, g.ibuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, g.idxCount, 1, 0, 0, 0);
}

void Renderer::rebuildTonemapSet(VkApp& app) {
    vkResetDescriptorPool(m_dev, m_tmPool, 0);
    VkDescriptorSetAllocateInfo sai{};
    sai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sai.descriptorPool = m_tmPool;
    sai.descriptorSetCount = 1;
    sai.pSetLayouts = &m_tmDsl;
    VK(vkAllocateDescriptorSets(m_dev, &sai, &m_tmSet));
    VkDescriptorImageInfo ii;
    ii.sampler = app.hdrSampler();
    ii.imageView = app.hdrView();
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo bi{m_tmUbuf, 0, 16};
    VkWriteDescriptorSet w[2]{};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = m_tmSet;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &ii;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = m_tmSet;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[1].pBufferInfo = &bi;
    vkUpdateDescriptorSets(m_dev, 2, w, 0, nullptr);
    m_tmSrcView = app.hdrView();
}

void Renderer::draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui,
                    float exposure) {
    writeViewUbo();
    for (auto& inst : m_insts) {
        const ModelUbo mb = packModel(inst.model, inst.baseColor, inst.metallic, inst.roughness);
        vkWriteBuffer(m_dev, inst.modelMem, &mb, sizeof(ModelUbo));
    }
    const VkExtent2D ext = app.extent();
    VkViewport vpt{0, (float)ext.height, (float)ext.width, -(float)ext.height, 0, 1};
    VkRect2D sc{0, 0, ext.width, ext.height};

    renderShadowCube(cmd);

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

    if (app.hdrView() != m_tmSrcView)
        rebuildTonemapSet(app);
    const float tm[4] = {(float)ext.width, (float)ext.height, exposure, 0.0f};
    vkWriteBuffer(m_dev, m_tmMem, tm, 16);

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
    for (uint32_t f = 0; f < 6; ++f) {
        vkDestroyFramebuffer(m_dev, m_shadowFb[f], nullptr);
        vkDestroyImageView(m_dev, m_shadowColorFace[f], nullptr);
        vkDestroyImageView(m_dev, m_shadowDepthFace[f], nullptr);
    }
    if (m_shadowPipe)
        vkDestroyPipeline(m_dev, m_shadowPipe, nullptr);
    if (m_shadowPl)
        vkDestroyPipelineLayout(m_dev, m_shadowPl, nullptr);
    if (m_shadowRp)
        vkDestroyRenderPass(m_dev, m_shadowRp, nullptr);
    if (m_shadowSampler)
        vkDestroySampler(m_dev, m_shadowSampler, nullptr);
    if (m_shadowCubeView)
        vkDestroyImageView(m_dev, m_shadowCubeView, nullptr);
    if (m_shadowCube) {
        vkDestroyImage(m_dev, m_shadowCube, nullptr);
        vkFreeMemory(m_dev, m_shadowCubeMem, nullptr);
    }
    if (m_shadowDepth) {
        vkDestroyImage(m_dev, m_shadowDepth, nullptr);
        vkFreeMemory(m_dev, m_shadowDepthMem, nullptr);
    }
}
