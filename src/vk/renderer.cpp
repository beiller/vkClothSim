// renderer.cpp
#include "vk/renderer.hpp"

#include "app/scene.hpp"
#include "ball_frag_spv.hpp"
#include "ball_vert_spv.hpp"
#include "caps_frag_spv.hpp"
#include "caps_vert_spv.hpp"
#include "cloth_frag_spv.hpp"
#include "cloth_vert_spv.hpp"
#include "sim/softsim.hpp"
#include "vk/vkapp.hpp"
#include "vk/vkutil.hpp"
#include "vk_bg_frag_spv.hpp"
#include "vk_bg_vert_spv.hpp"
#include <cmath>
#include <cstring>
#include <imgui_impl_vulkan.h>

namespace {

// Create a graphics pipeline with the shared state: triangle list, no culling, dynamic
// viewport/scissor, no blend, MSAA off. `depthTest` toggles depth test + write (the
// background is drawn without it; the meshes with it). The vertex input (bindings +
// attributes, or none) comes from the caller; the shaders are the baked SPIR-V.
VkPipeline makeGraphicsPipeline(VkDevice dev, VkRenderPass rp, const void* vsSpv, uint32_t vsLen, const void* fsSpv,
                                uint32_t fsLen, VkPipelineLayout layout, bool depthTest,
                                const VkVertexInputBindingDescription* binds, uint32_t nBinds,
                                const VkVertexInputAttributeDescription* attrs, uint32_t nAttrs) {
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

// Build all capsules' geometry (cylinder + 2 hemispheres) into one VBO/IBO.
// Each vertex: pos(3) nrm(3) capsuleIndex(1) = 7 floats (28 bytes). The ground quad is
// appended in world space, flagged aIdx = -1. `R`/`H` are the shared capsule dimensions.
void buildCapsuleGeometry(std::vector<float>& verts, std::vector<uint32_t>& idx, int nCaps, float R, float H) {
    const int S = 20, M = 32;
    const float twoPi = 2.0f * kPi;
    for (int ci = 0; ci < nCaps; ++ci) {
        float top = H + R, bot = -H - R;
        auto capBase = (uint32_t)(verts.size() / 7);
        for (int i = 0; i <= M; ++i) {
            float t = (float)i / M;
            float y = top - t * (top - bot);
            float r, dr;
            float dy;
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
                idx.push_back(a);
                idx.push_back(b);
                idx.push_back(d);
                idx.push_back(a);
                idx.push_back(d);
                idx.push_back(c);
            }
    }
    // ground quad (world-space, flagged aIdx = -1)
    auto gbase = (uint32_t)(verts.size() / 7);
    float G = 55.0f;
    const float gv[4][3] = {{-G, 0.0f, -G}, {G, 0.0f, -G}, {G, 0.0f, G}, {-G, 0.0f, G}};
    for (const auto& v : gv) {
        verts.push_back(v[0]);
        verts.push_back(v[1]);
        verts.push_back(v[2]);
        verts.push_back(0.0f);
        verts.push_back(1.0f);
        verts.push_back(0.0f);
        verts.push_back(-1.0f);
    }
    idx.push_back(gbase);
    idx.push_back(gbase + 1);
    idx.push_back(gbase + 2);
    idx.push_back(gbase);
    idx.push_back(gbase + 2);
    idx.push_back(gbase + 3);
}

// viewProj + a body-specific tail into an 80-byte mesh UBO. viewProj + the tail are constant
// for the life of the app, so this is written ONCE at init (not per frame).
void writeUbo(VkDevice dev, VkDeviceMemory mem, const Mat4& vp, const float* tail, size_t tailBytes) {
    void* up;
    VK(vkMapMemory(dev, mem, 0, 80, 0, &up));
    std::memcpy(up, vp.m, 64);
    std::memcpy((char*)up + 64, tail, tailBytes);
    vkUnmapMemory(dev, mem);
}

} // namespace

void Renderer::init(VkApp& app, const Scene& scene, const SoftSim& sim, const Mat4& viewProj) {
    m_dev = app.device();
    m_pdev = app.pdev();
    m_rp = app.renderPass();
    m_vp = viewProj;
    m_nCaps = sim.capsuleCount();
    initBackground();
    initCapsules(sim);
    initSoftBodies(scene, sim);
}

void Renderer::initBackground() {
    VkDevice dev = m_dev;
    // 16-byte color UBO + the full-screen triangle vertex buffer (explicit vertices,
    // robust on all drivers)
    vkMakeBuffer(dev, m_pdev, m_bgUbuf, m_bgUmem, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    float verts[6] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
    vkMakeBuffer(dev, m_pdev, m_bgVbuf, m_bgVmem, sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, verts);

    // 0: the color UBO (fragment)
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    vkMakeDslPool(dev, {b}, 1, m_bgDsl, m_bgPool);
    vkMakeSet(dev, m_bgPool, m_bgDsl, {b}, m_bgSet, {VkDescriptorBufferInfo{m_bgUbuf, 0, 16}});
    m_bgPl = vkMakePipelineLayout(dev, m_bgDsl);

    VkVertexInputBindingDescription bind{0, 8, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr{0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    m_bgPipe = makeGraphicsPipeline(dev, m_rp, vk_bg_vert_spv, vk_bg_vert_spv_len / 4, vk_bg_frag_spv,
                                    vk_bg_frag_spv_len / 4, m_bgPl, /*depthTest=*/false, &bind, 1, &attr, 1);
}

void Renderer::initCapsules(const SoftSim& sim) {
    VkDevice dev = m_dev;
    std::vector<float> verts;
    std::vector<uint32_t> idx;
    buildCapsuleGeometry(verts, idx, m_nCaps, RigidScene::kCapsuleRadius, RigidScene::kCapsuleHalfLen);
    m_caps.idxCount = (uint32_t)idx.size();
    vkMakeBuffer(dev, m_pdev, m_caps.vbuf, m_caps.vbmem, (VkDeviceSize)verts.size() * 4,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, verts.data());
    vkMakeBuffer(dev, m_pdev, m_caps.ibuf, m_caps.ibmem, (VkDeviceSize)idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 idx.data());
    vkMakeBuffer(dev, m_pdev, m_caps.ubuf, m_caps.ubmem, 80, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    // the capsule UBO (viewProj + the cloth grid dims the capsule shader ignores) is constant,
    // so it is written once here (not per frame).
    float extra[2] = {Scene::kCW, Scene::kCH};
    writeUbo(dev, m_caps.ubmem, m_vp, extra, sizeof(extra));

    // bindings: 0 viewProj+grid UBO, 1 the sim's capsule collider SSBO (the instanced draw)
    VkDescriptorSetLayoutBinding binds[2] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
    };
    vkMakeDslPool(dev, {binds, binds + 2}, 1, m_caps.dsl, m_caps.pool);
    vkMakeSet(dev, m_caps.pool, m_caps.dsl, {binds, binds + 2}, m_caps.set,
              {VkDescriptorBufferInfo{m_caps.ubuf, 0, 80}, {sim.capsulesBuffer(), 0, 48 * (VkDeviceSize)m_nCaps}});
    m_caps.pl = vkMakePipelineLayout(dev, m_caps.dsl);

    // vertex input: pos(3) nrm(3) capsuleIndex(1) = 28 bytes
    VkVertexInputBindingDescription bind{0, 28, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32_SFLOAT, 24},
    };
    m_caps.pipe = makeGraphicsPipeline(dev, m_rp, caps_vert_spv, caps_vert_spv_len / 4, caps_frag_spv,
                                       caps_frag_spv_len / 4, m_caps.pl, /*depthTest=*/true, &bind, 1, attrs, 3);
}

// Build both soft bodies' render meshes (the cloth grid + the ball sphere): the index buffer +
// the 80-byte UBO + the descriptor set (binding 0 = the sim's position buffer) + the pipeline.
void Renderer::initSoftBodies(const Scene& scene, const SoftSim& sim) {
    { // the cloth: a grid mesh (render). The index buffer tiles the CW x CH grid.
        Mesh& m = m_clothMesh;
        std::vector<uint32_t> idx;
        for (int gy = 0; gy < Scene::kCH - 1; ++gy)
            for (int gx = 0; gx < Scene::kCW - 1; ++gx) {
                auto a = (uint32_t)(gy * Scene::kCW + gx);
                idx.push_back(a);
                idx.push_back(a + 1);
                idx.push_back(a + Scene::kCW);
                idx.push_back(a + 1);
                idx.push_back(a + Scene::kCW + 1);
                idx.push_back(a + Scene::kCW);
            }
        float extra[2] = {Scene::kCW, Scene::kCH}; // the grid dims (the shader tiles them)
        makeBodyRenderMesh(m, sim, SoftSim::kCloth, idx, cloth_vert_spv, cloth_vert_spv_len / 4, cloth_frag_spv,
                           cloth_frag_spv_len / 4, extra, sizeof(extra));
    }
    { // the ball: a sphere mesh (render). The index buffer is the UV-sphere triangles.
        Mesh& m = m_ballMesh;
        std::vector<uint32_t> idx;
        for (const auto& t : scene.ballTris()) {
            idx.push_back(t.a);
            idx.push_back(t.b);
            idx.push_back(t.c);
        }
        float extra[3] = {0, 0, 0}; // center unused (the normal is a derivative)
        makeBodyRenderMesh(m, sim, SoftSim::kBall, idx, ball_vert_spv, ball_vert_spv_len / 4, ball_frag_spv,
                           ball_frag_spv_len / 4, extra, sizeof(extra));
    }
}

// The cloth + ball share the same descriptor layout (0: pos SSBO, 1: UBO): create the
// descriptor set layout + pool + set + pipeline layout for one of them. `posBuf` (owned by
// the sim) is binding 0; it is re-pointed each frame by updateSoftRenderSet.
void Renderer::makePosUboDescriptorSet(VkDevice dev, Mesh& m, VkBuffer posBuf, VkDeviceSize posRange) {
    VkDescriptorSetLayoutBinding rb[2] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
    };
    vkMakeDslPool(dev, {rb, rb + 2}, 1, m.dsl, m.pool);
    vkMakeSet(dev, m.pool, m.dsl, {rb, rb + 2}, m.set, {VkDescriptorBufferInfo{posBuf, 0, posRange}, {m.ubuf, 0, 80}});
    m.pl = vkMakePipelineLayout(dev, m.dsl);
}

void Renderer::makeBodyRenderMesh(Mesh& m, const SoftSim& sim, SoftSim::Body body, const std::vector<uint32_t>& idx,
                                  const void* vertSpv, uint32_t vertLen, const void* fragSpv, uint32_t fragLen,
                                  const float* extraUbo, size_t extraUboBytes) {
    VkDevice dev = m_dev;
    m.idxCount = (uint32_t)idx.size();
    m.sbuf = VK_NULL_HANDLE; // the position buffer is owned by the sim (never destroyed here)
    vkMakeBuffer(dev, m_pdev, m.ibuf, m.ibmem, (VkDeviceSize)idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 idx.data());
    vkMakeBuffer(dev, m_pdev, m.ubuf, m.ubmem, 80, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    makePosUboDescriptorSet(dev, m, sim.posBuffer(body), (VkDeviceSize)16 * sim.vertexCount(body));
    m.pipe = makeGraphicsPipeline(dev, m_rp, vertSpv, vertLen, fragSpv, fragLen, m.pl, /*depthTest=*/true, nullptr, 0,
                                  nullptr, 0);
    writeUbo(dev, m.ubmem, m_vp, extraUbo, extraUboBytes); // viewProj + the body tail (constant)
}

// Point a soft-body render descriptor at the sim's current position buffer (re-done each
// frame, since the sim ping-pongs posA/posB).
void Renderer::updateSoftRenderSet(Mesh& m, const SoftSim& sim, SoftSim::Body body) {
    VkDescriptorBufferInfo bi{sim.posBuffer(body), 0, (VkDeviceSize)16 * sim.vertexCount(body)};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = m.set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(m_dev, 1, &w, 0, nullptr);
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

void Renderer::draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui,
                    const SoftSim& sim) {
    // the background color (a UI param)
    {
        void* p;
        VK(vkMapMemory(m_dev, m_bgUmem, 0, 16, 0, &p));
        float col[4] = {bg[0], bg[1], bg[2], 1.0f};
        std::memcpy(p, col, 16);
        vkUnmapMemory(m_dev, m_bgUmem);
    }
    // the sim ran just before this (same command buffer): re-point the soft-body draws at the
    // sim's current position buffers (the sim ping-pongs posA/posB).
    updateSoftRenderSet(m_clothMesh, sim, SoftSim::kCloth);
    updateSoftRenderSet(m_ballMesh, sim, SoftSim::kBall);
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
    drawMesh(cmd, m_clothMesh, nullptr);
    drawMesh(cmd, m_ballMesh, nullptr);
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
    destroyMesh(m_clothMesh); // its sbuf is VK_NULL_HANDLE (the pos buffer is sim-owned)
    destroyMesh(m_ballMesh);
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
