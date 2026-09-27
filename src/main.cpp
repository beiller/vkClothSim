#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "demo.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include "world.hpp"
#include <chrono>
#include <cstdio>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

int main() {
    VkApp app;
    if (!app.init(900, 900, "3dsim"))
        return 1;

    World w;
    w.app = &app;
    createDemoWorld(w);
    auto makeVP = [&w](VkExtent2D ext) {
        return w.camera.viewProj((float)ext.width / (float)ext.height);
    };

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(app.glfwWindow(), true);
    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_1;
    ii.Instance = app.instance();
    ii.PhysicalDevice = app.pdev();
    ii.Device = app.device();
    ii.QueueFamily = app.queueFamily();
    ii.Queue = app.queue();
    ii.DescriptorPoolSize = 1024;
    ii.MinImageCount = app.imageCount();
    ii.ImageCount = app.imageCount();
    ii.PipelineInfoMain.RenderPass = app.renderPass();
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS)
            std::printf("ImGui VK err %d\n", (int)err);
    };
    ImGui_ImplVulkan_Init(&ii);

    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | R to reset | esc/close to quit\n");

    auto lastFrame = std::chrono::steady_clock::now();
    while (!app.windowShouldClose()) {
        const auto nowFrame = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(nowFrame - lastFrame).count();
        lastFrame = nowFrame;
        if (app.keyIsDown(GLFW_KEY_R))
            resetSofts(w);
        stepPinHolds(w, dt);

        w.rigid.setVelocitySteps(w.ui.joltIters);
        stepRigid(w);
        syncColliders(w);
        VkCommandBuffer simCmd = app.beginCommands();
        stepSoft(w, simCmd);
        app.submit(simCmd);

        app.pollEvents();
        w.renderer.setViewProj(makeVP(app.extent()), w.camera.position);
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (w.drawUi)
            w.drawUi(w);
        ImGui::Render();

        const uint32_t idx = app.acquireNextImage();
        VkCommandBuffer cmd = app.beginCommands();
        draw(w, cmd, idx, w.ui.bgColor, ImGui::GetDrawData());
        app.present(idx);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    w.sim.shutdown();
    w.renderer.shutdown();
    app.shutdown();
    return 0;
}
