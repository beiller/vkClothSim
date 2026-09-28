#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "demo.hpp"
#include "hdri.hpp"
#include "shadowtest.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include "world.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

int main(int argc, char** argv) {
    VkApp app;
    if (!app.init(900, 900, "3dsim"))
        return 1;

    World w;
    w.app = &app;
    bool sceneSet = false, dumpFrames = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hdri") == 0) {
            createHdriWorld(w);
            sceneSet = true;
        } else if (std::strcmp(argv[i], "--shadow") == 0) {
            createShadowTestWorld(w);
            sceneSet = true;
        } else if (std::strcmp(argv[i], "--dump") == 0)
            dumpFrames = true;
    }
    if (!sceneSet)
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

    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | R reset | S dump shadow map | esc/close to quit\n");

    auto lastFrame = std::chrono::steady_clock::now();
    bool dumpWasDown = false;
    int frame = 0;
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
        const bool dumpDown = app.keyIsDown(GLFW_KEY_S);
        if (dumpDown && !dumpWasDown)
            w.renderer.dumpShadowMap(app, "/tmp/shadow");
        dumpWasDown = dumpDown;
        if (dumpFrames && frame < 3) {
            char prefix[64];
            std::snprintf(prefix, sizeof(prefix), "/tmp/shadow_f%d", frame);
            w.renderer.dumpShadowMap(app, prefix);
        }
        ++frame;
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
