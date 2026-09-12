// main.cpp
// The app: the main loop that ties the pieces together. VkApp owns the window + the
// Vulkan core; Scene owns the simulation data (the rigid capsules + the soft cloth/ball
// initial state); SoftSim owns the GPU soft-body sim (steps the cloth + the ball);
// Renderer owns the GPU graphics (pipelines + buffers + the per-frame draw, reading the
// sim's output); ui.{hpp,cpp} the ImGui overlay.
#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "app/scene.hpp"
#include "app/ui.hpp"
#include "math.hpp"
#include "sim/softsim.hpp"
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

    // the simulation (the rigid capsules + the soft cloth/ball initial state)
    Scene scene;
    scene.init();

    // the GPU soft-body sim (the "cloth sim": steps the cloth + the ball on the GPU)
    SoftSim sim;
    sim.init(app.device(), app.pdev(), scene.cloth(), scene.ball(), scene.rigid().capsuleCount());

    // fixed camera looking at the capsule field
    const float aspect = (float)app.extent().width / (float)app.extent().height;
    const Mat4 vp = mul4(perspective(50.0f, aspect, 0.1f, 300.0f),
                         lookAt({0.0f, 9.0f, 14.0f}, {0.0f, 3.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));

    // the GPU graphics (the pipelines + the per-frame draws; reads the sim's output)
    Renderer renderer;
    renderer.init(app, scene, sim, vp);

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
            sim.resetSoftBodies();
        }
        scene.stepRigid(1);
        auto caps = scene.rigid().capsuleGPU(); // the Jolt capsule transforms (CPU -> GPU)
        sim.uploadCapsules(caps);               // -> GPU (the sim reads them as colliders)
        // the ImGui overlay
        app.pollEvents();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        bool clothReset = false, ballReset = false;
        drawOverlay(ui, scene.clothPinned(), clothReset, ballReset);
        if (clothReset) {
            scene.reset();
            sim.resetSoftBodies();
        }
        if (ballReset)
            sim.resetBall();
        ImGui::Render();
        // one command buffer: the GPU soft-body sim (record) first, then the render + present
        uint32_t idx = app.acquireNextImage();
        VkCommandBuffer cmd = app.beginCommands();
        sim.record(cmd, ui.sim, scene.clothPinned() ? 1 : 0);                      // the GPU soft-body sim
        renderer.draw(cmd, app, idx, ui.bgColor, ImGui::GetDrawData(), sim, caps); // the render
        app.present(idx);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    renderer.shutdown(); // destroys the descriptor sets that reference the sim's buffers
    sim.shutdown();      // destroys the sim's buffers (positions + capsules) + the compute pipeline
    app.shutdown();
    return 0;
}
