#include "vk/renderer.hpp"

#include "mesh_frag_spv.hpp"
#include "mesh_vert_spv.hpp"
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
    InstancedMesh& im = m_insts[inst];
    if (im.tex[channel] == texId)
        return;
    im.tex[channel] = texId;
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
