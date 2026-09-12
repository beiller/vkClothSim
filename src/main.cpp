// main.cpp
// The app: the main loop that ties the pieces together. VkApp owns the window + the
// Vulkan core; Scene owns the simulation (the rigid capsules + the soft cloth/ball);
// Renderer owns the GPU (pipelines + buffers + the per-frame draw); ui.{hpp,cpp} the
// ImGui overlay.
#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "app/scene.hpp"
#include "app/ui.hpp"
#include "math.hpp"
#include "vk/renderer.hpp"
#include "vk/vkapp.hpp"
#include <cstdio>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

int main() {
    // the window + the Vulkan core
    VkApp app;
    if (!app.init(900, 900, "3dsim"))
        return 1;

    // the simulation (the rigid capsules + the soft cloth/ball)
    Scene scene;
    scene.init();

    // fixed camera looking at the capsule field
    const float aspect = (float)app.extent().width / (float)app.extent().height;
    const Mat4 vp = mul4(perspective(50.0f, aspect, 0.1f, 300.0f),
                         lookAt({0.0f, 9.0f, 14.0f}, {0.0f, 3.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));

    // the GPU (the pipelines + the per-frame uploads/draws)
    Renderer renderer;
    renderer.init(app, scene, vp);

    // the ImGui context + the GLFW/Vulkan backends (drawn into the same swapchain)
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

    UIState ui;

    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | R to reset | esc/close to quit\n");
    while (!app.windowShouldClose()) {
        // input + physics (the rigid capsules on the CPU; the soft bodies step on the GPU)
        if (app.keyIsDown(GLFW_KEY_R)) {
            scene.reset();
            renderer.resetSoftBodies();
        }
        scene.stepRigid(1);
        renderer.uploadCapsules(scene.rigid().capsuleGPU()); // -> GPU (the sim + render read them)
        // the ImGui overlay
        app.pollEvents();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        bool clothReset = false, ballReset = false;
        drawOverlay(ui, scene.clothPinned(), clothReset, ballReset);
        if (clothReset) {
            scene.reset();
            renderer.resetSoftBodies();
        }
        if (ballReset)
            renderer.resetBall();
        ImGui::Render();
        // the GPU soft-body sim (recordSoftSim) + the render + present (one command buffer)
        uint32_t idx = app.acquireNextImage();
        renderer.draw(app, idx, ui.bgColor, ImGui::GetDrawData(), ui.sim, scene.clothPinned() ? 1 : 0);
        app.present(idx);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    renderer.shutdown();
    app.shutdown();
    return 0;
}
