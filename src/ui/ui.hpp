#pragma once
#include "ecs.hpp"
#include <functional>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <imgui.h>

// ImGui host: one context per canvas (UICanvas entity). Canvas windows are pinned to
// (0,0) and fill the canvas; input is routed per canvas by the app.
struct UiHandles {
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice pdev = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    int imageCount = 2;
    std::function<VkImage(entt::entity)> canvasImage;
};

void uiInit(const UiHandles& h);
void uiShutdown();
void uiSetButtonHook(std::function<void(const std::string&, entt::entity)> fn);
void uiSetChangeHook(std::function<void(const std::string&, entt::entity)> fn);

struct UiFeed {
    entt::entity canvas = entt::null;
    ImVec2 pos{-1.0f, -1.0f};
    bool down = false;
    float wheel = 0.0f;
    bool demo = false;
};

// canvas target (FBO) must exist (GraphicsServer::uiEnsureTarget); called after it
void uiEnsureContext(const entt::registry& reg, entt::entity canvas, VkRenderPass rp, VkFramebuffer fb);
void uiFrame(entt::registry& reg, const std::vector<UiFeed>& feeds);
void uiRender(entt::entity canvas, VkCommandBuffer cmd);
