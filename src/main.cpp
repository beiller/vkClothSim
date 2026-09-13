#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "app/scene.hpp"
#include "app/ui.hpp"
#include "math.hpp"
#include "sim/softsim.hpp"
#include "vk/renderer.hpp"
#include "vk/vkapp.hpp"
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

int main(int argc, char** argv) {
    bool single = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--single") == 0)
            single = true;

    VkApp app;
    if (!app.init(900, 900, "3dsim"))
        return 1;

    Scene scene;
    scene.initRigid(single);
    const float span = single ? Scene::kSingleSpan : Scene::kClothSpan;
    const float y0 = single ? Scene::kSingleY0 : Scene::kClothY0;
    const int clothIdx = scene.add(makeCloth(Scene::kCW, Scene::kCH, span, y0), true);
    const int ballIdx =
        scene.add(makeBall(Scene::kBallLat, Scene::kBallLon, Scene::kBallRadius, Scene::kBallY0), false);
    const int allBodies = (1 << scene.size()) - 1;

    SoftSim sim;
    sim.init(app.device(), app.pdev(), scene.rigid().capsuleCount());
    for (const auto& s : scene.softs())
        sim.registerBody(s.mesh.mesh, s.mesh.cons);
    sim.build();

    const float aspect = (float)app.extent().width / (float)app.extent().height;
    const Mat4 vp = mul4(perspective(50.0f, aspect, 0.1f, 300.0f),
                         lookAt({0.0f, 9.0f, 14.0f}, {0.0f, 3.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));

    Renderer renderer;
    renderer.init(app, sim.draws(), scene.rigid().capsuleCount(), vp);

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
        if (app.keyIsDown(GLFW_KEY_R)) {
            scene.reset();
            sim.reset(allBodies);
        }
        scene.stepRigid(1);
        auto caps = scene.rigid().capsuleGPU();
        sim.uploadCapsules(caps);

        app.pollEvents();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        bool clothReset = false, ballReset = false;
        drawOverlay(ui, scene.isPinned(clothIdx), clothReset, ballReset);
        if (clothReset) {
            scene.reset();
            sim.reset(allBodies);
        }
        if (ballReset)
            sim.reset(1 << ballIdx);
        ImGui::Render();

        uint32_t idx = app.acquireNextImage();
        VkCommandBuffer cmd = app.beginCommands();
        sim.record(cmd, ui.sim, scene.pinnedMask());
        app.submit(cmd);
        cmd = app.beginCommands();
        renderer.draw(cmd, app, idx, ui.bgColor, ImGui::GetDrawData(), caps);
        app.present(idx);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    renderer.shutdown();
    sim.shutdown();
    app.shutdown();
    return 0;
}
