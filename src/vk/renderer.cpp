// renderer.cpp
#include "vk/renderer.hpp"

#include "app/rigid.hpp"
#include "app/scene.hpp"
#include "mesh_frag_spv.hpp"
#include "mesh_vert_spv.hpp"
#include "sim/softsim.hpp"
#include "vk/vkapp.hpp"
#include "vk/vkutil.hpp"
#include <cmath>
#include <cstring>
#include <imgui_impl_vulkan.h>

namespace {

// The render DSL's 2 bindings, shared by every mesh (0: the Vtx SSBO, 1: the viewProj UBO).
std::vector<VkDescriptorSetLayoutBinding> meshBinds() {
    return {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
    };
}

// Create the ONE graphics pipeline: triangle list, no culling, dynamic viewport/scissor, no
// blend, MSAA off, depth test+write on, and NO vertex input (the vertices come from the Vtx
// SSBO indexed by gl_VertexIndex). The shaders are the baked SPIR-V (mesh_vert + mesh_frag).
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

// The ground quad as render Vtx (pos + nrm + col, 48 bytes each) + its index list. The
// normals face +Y and the color is the ground's albedo. Static (built once at init).
void buildGroundVtx(std::vector<float>& vtx, std::vector<uint32_t>& idx) {
    const float G = 55.0f;
    const float gv[4][3] = {{-G, 0.0f, -G}, {G, 0.0f, -G}, {G, 0.0f, G}, {-G, 0.0f, G}};
    const float COL[3] = {0.19f, 0.21f, 0.17f};
    vtx.clear();
    for (const auto& v : gv) {
        vtx.push_back(v[0]);
        vtx.push_back(v[1]);
        vtx.push_back(v[2]);
        vtx.push_back(0.0f); // pos
        vtx.push_back(0.0f);
        vtx.push_back(1.0f);
        vtx.push_back(0.0f);
        vtx.push_back(0.0f); // nrm (up)
        vtx.push_back(COL[0]);
        vtx.push_back(COL[1]);
        vtx.push_back(COL[2]);
        vtx.push_back(0.0f); // col
    }
    idx = {0, 1, 2, 0, 2, 3};
}

// Build each capsule's LOCAL geometry (position + normal per vertex, 6 floats each) + the
// shared index list. Capsule `ci`'s vertices occupy [ci*VPC, ci*VPC+VPC). `VPC` = (M+1)*S.
// The per-frame bake (Renderer::bakeCapsules) applies each capsule's rotation + center + the
// red albedo to turn this into the world-space Vtx buffer.
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
                base[6 * v + 0] = r * cp; // local pos
                base[6 * v + 1] = y;
                base[6 * v + 2] = r * sp;
                base[6 * v + 3] = cp; // local nrm
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

// quaternion -> 3x3 rotation matrix (matches the shader's quatMat; the capsule bake uses it).
void quatToMat3(const float q[4], float m[9]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1.0f - 2.0f * (y * y + z * z);
    m[1] = 2.0f * (x * y + z * w);
    m[2] = 2.0f * (x * z - y * w);
    m[3] = 2.0f * (x * y - z * w);
    m[4] = 1.0f - 2.0f * (x * x + z * z);
    m[5] = 2.0f * (y * z + x * w);
    m[6] = 2.0f * (x * z + y * w);
    m[7] = 2.0f * (y * z - x * w);
    m[8] = 1.0f - 2.0f * (x * x + y * y);
}

} // namespace

void Renderer::init(VkApp& app, const Scene& scene, const SoftSim& sim, const Mat4& viewProj) {
    m_dev = app.device();
    m_pdev = app.pdev();
    m_rp = app.renderPass();
    m_vp = viewProj;
    m_nCaps = sim.capsuleCount();

    // the ONE shared viewProj UBO (64 bytes; the camera is fixed, so written once at init).
    vkMakeBuffer(m_dev, m_pdev, m_uUbuf, m_uUmem, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, viewProj.m);

    // the ONE render DSL (0: Vtx SSBO, 1: UBO) + pool (4 sets) + pipeline layout + pipeline.
    const auto binds = meshBinds();
    vkMakeDslPool(m_dev, binds, /*maxSets=*/4, m_dsl, m_pool);
    m_pl = vkMakePipelineLayout(m_dev, m_dsl);
    m_pipe = makeGraphicsPipeline(m_dev, m_rp, mesh_vert_spv, mesh_vert_spv_len / 4, mesh_frag_spv,
                                  mesh_frag_spv_len / 4, m_pl);

    initGround();
    initCapsules();
    initSoftBodies(scene, sim);
}

void Renderer::initGround() {
    std::vector<float> vtx;
    std::vector<uint32_t> idx;
    buildGroundVtx(vtx, idx);
    vkMakeBuffer(m_dev, m_pdev, m_groundVtx, m_groundVtxMem, (VkDeviceSize)vtx.size() * 4,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vtx.data());
    makeMesh(m_ground, idx, m_groundVtx, (VkDeviceSize)vtx.size() * 4);
}

void Renderer::initCapsules() {
    const int S = 20, M = 32;
    m_vpc = (uint32_t)((M + 1) * S);
    std::vector<uint32_t> idx;
    buildCapsuleBase(m_capsBase, idx, m_nCaps, RigidScene::kCapsuleRadius, RigidScene::kCapsuleHalfLen);
    const size_t vcount = (size_t)m_nCaps * m_vpc;
    vkMakeBuffer(m_dev, m_pdev, m_capsVtx, m_capsVtxMem, (VkDeviceSize)48 * vcount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 nullptr);
    makeMesh(m_caps, idx, m_capsVtx, (VkDeviceSize)48 * vcount);
}

// Build the two soft bodies' render meshes (the cloth grid + the ball sphere): the index
// buffer + the descriptor set (binding 0 = the sim's current Vtx buffer, re-pointed per
// frame). The Vtx SSBO is owned by the sim (never destroyed here).
void Renderer::initSoftBodies(const Scene& scene, const SoftSim& sim) {
    { // the cloth: tile the CW x CH grid.
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
        makeMesh(m_cloth, idx, sim.posBuffer(SoftSim::kCloth), (VkDeviceSize)48 * sim.vertexCount(SoftSim::kCloth));
    }
    { // the ball: the UV-sphere triangles.
        std::vector<uint32_t> idx;
        for (const auto& t : scene.ballTris()) {
            idx.push_back(t.a);
            idx.push_back(t.b);
            idx.push_back(t.c);
        }
        makeMesh(m_ball, idx, sim.posBuffer(SoftSim::kBall), (VkDeviceSize)48 * sim.vertexCount(SoftSim::kBall));
    }
}

void Renderer::makeMesh(Mesh& m, const std::vector<uint32_t>& idx, VkBuffer vtxBuf, VkDeviceSize vtxRange) {
    m.idxCount = (uint32_t)idx.size();
    vkMakeBuffer(m_dev, m_pdev, m.ibuf, m.ibmem, (VkDeviceSize)idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 idx.data());
    std::vector<VkDescriptorBufferInfo> bi = {
        {vtxBuf, 0, vtxRange},
        {m_uUbuf, 0, 64},
    };
    vkMakeSet(m_dev, m_pool, m_dsl, meshBinds(), m.set, bi);
}

void Renderer::updateSoftRenderSet(Mesh& m, const SoftSim& sim, SoftSim::Body body) {
    VkDescriptorBufferInfo bi{sim.posBuffer(body), 0, (VkDeviceSize)48 * sim.vertexCount(body)};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = m.set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(m_dev, 1, &w, 0, nullptr);
}

void Renderer::bakeCapsules(const std::vector<CapsuleGPU>& caps) {
    const float COL[3] = {0.85f, 0.35f, 0.30f}; // the capsule's albedo (red)
    void* o;
    VK(vkMapMemory(m_dev, m_capsVtxMem, 0, (VkDeviceSize)48 * m_nCaps * m_vpc, 0, &o));
    auto* out = static_cast<float*>(o);
    for (size_t ci = 0; ci < caps.size(); ++ci) {
        const float q[4] = {caps[ci].quat[0], caps[ci].quat[1], caps[ci].quat[2], caps[ci].quat[3]};
        float m[9];
        quatToMat3(q, m);
        const float cx = caps[ci].centerRadius[0], cy = caps[ci].centerRadius[1], cz = caps[ci].centerRadius[2];
        for (size_t j = 0; j < m_vpc; ++j) {
            size_t v = ci * m_vpc + j;
            const float* bp = &m_capsBase[6 * v];
            float* op = &out[12 * v];
            float lp0 = bp[0], lp1 = bp[1], lp2 = bp[2];
            float ln0 = bp[3], ln1 = bp[4], ln2 = bp[5];
            // R * v for the column-major `m` (m[col*3+row] = M[row][col]): (R*v).row =
            // sum_col M[row][col] * v[col] = sum_col m[col*3+row] * v[col].
            op[0] = m[0] * lp0 + m[3] * lp1 + m[6] * lp2 + cx;
            op[1] = m[1] * lp0 + m[4] * lp1 + m[7] * lp2 + cy;
            op[2] = m[2] * lp0 + m[5] * lp1 + m[8] * lp2 + cz;
            op[3] = 0.0f;
            op[4] = m[0] * ln0 + m[3] * ln1 + m[6] * ln2;
            op[5] = m[1] * ln0 + m[4] * ln1 + m[7] * ln2;
            op[6] = m[2] * ln0 + m[5] * ln1 + m[8] * ln2;
            op[7] = 0.0f;
            op[8] = COL[0];
            op[9] = COL[1];
            op[10] = COL[2];
            op[11] = 0.0f;
        }
    }
    vkUnmapMemory(m_dev, m_capsVtxMem);
}

void Renderer::drawMesh(VkCommandBuffer cmd, VkPipelineLayout pl, const Mesh& m) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &m.set, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, m.ibuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m.idxCount, 1, 0, 0, 0);
}

void Renderer::draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const float bg[3], ImDrawData* imgui,
                    const SoftSim& sim, const std::vector<CapsuleGPU>& caps) {
    // the sky is the render pass' clear color (a UI param) — no background shader.
    // the sim ran just before this (same command buffer): bake the capsules + re-point the
    // soft-body draws at the sim's current Vtx buffers (the sim ping-pongs posA/posB).
    bakeCapsules(caps);
    updateSoftRenderSet(m_cloth, sim, SoftSim::kCloth);
    updateSoftRenderSet(m_ball, sim, SoftSim::kBall);
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
    // every mesh shares the one pipeline + pipeline layout: bind it once, then draw each.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipe);
    drawMesh(cmd, m_pl, m_ground);
    drawMesh(cmd, m_pl, m_caps);
    drawMesh(cmd, m_pl, m_cloth);
    drawMesh(cmd, m_pl, m_ball);
    // the ImGui overlay (the same swapchain image; zero CPU pixel copy)
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
    // the descriptor sets are freed by the pool (destroyed below), matching the sim.
    auto destroyMesh = [this](Mesh& m) {
        if (m.ibuf) {
            vkDestroyBuffer(m_dev, m.ibuf, nullptr);
            vkFreeMemory(m_dev, m.ibmem, nullptr);
        }
    };
    destroyMesh(m_ground);
    destroyMesh(m_caps);
    destroyMesh(m_cloth); // its Vtx SSBO is sim-owned (not destroyed here)
    destroyMesh(m_ball);  // its Vtx SSBO is sim-owned (not destroyed here)
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
