#include "ecs.hpp"
#include "shadow_frag_spv.hpp"
#include "shadow_vert_spv.hpp"
#include "vk/renderer.hpp"
#include "vk/vkapp.hpp"
#include "vk/vkutil.hpp"
#include <algorithm>
#include <cstring>

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

void Renderer::renderShadowCubes(VkCommandBuffer cmd, const entt::registry& reg) {
    struct SL {
        V3 pos;
        float nearP, farP, normalBias;
    };
    SL sl[kMaxLights];
    int n = 0;
    reg.view<WorldTransform, PointLight>().each([&](auto, const WorldTransform& wt, const PointLight& p) {
        if (n < kMaxLights && p.on >= 0.5f)
            sl[n++] = {wt.pos, std::max(p.shadowNear, 0.001f),
                       std::max(p.shadowFar, std::max(p.shadowNear, 0.001f) + 0.01f), p.shadowNormalBias};
    });
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

    for (int i = 0; i < n; ++i) {
        const SL& L = sl[i];
        const Mat4 proj = perspective(90.0f, 1.0f, L.nearP, L.farP);
        const float farD = L.farP + 1.0f;

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
            pc.lightPosBias[3] = L.normalBias;

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
            for (auto [e, rend] : reg.view<Renderable>().each())
                drawInstance(cmd, m_shadowPl, m_geoms[m_insts[rend.inst].geom], m_insts[rend.inst]);
            vkCmdEndRenderPass(cmd);
        }
    }

    VkImageMemoryBarrier imb{};
    imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imb.image = m_shadowCube;
    imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, (uint32_t)(n * 6)};
    imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imb.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &imb);
}
