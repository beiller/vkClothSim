#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "scenes/demo.hpp"
#include "scenes/hdri.hpp"
#include "scenes/hierarchy.hpp"
#include "scenes/shadowtest.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include "world.hpp"
#include "xr/xr.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

int main(int argc, char** argv) {
    VkApp app;
    if (!app.initInstance(900, 900, "3dsim"))
        return 1;

    World w;
    w.app = &app;
    bool wantHdri = false, wantShadow = false, wantHier = false, dumpFrames = false, wantNoVr = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hdri") == 0)
            wantHdri = true;
        else if (std::strcmp(argv[i], "--shadow") == 0)
            wantShadow = true;
        else if (std::strcmp(argv[i], "--hier") == 0)
            wantHier = true;
        else if (std::strcmp(argv[i], "--dump") == 0)
            dumpFrames = true;
        else if (std::strcmp(argv[i], "--no-vr") == 0)
            wantNoVr = true;
    }
    // VR: auto-detect the OpenXR runtime (creates the Vulkan device through it, so this must
    // run before the scene); no runtime/HMD -> plain window mode. --no-vr skips detection.
    if (!wantNoVr) {
        w.xr = std::make_unique<Xr>();
        if (!w.xr->createDevice(app))
            w.xr.reset();
    }
    app.initDevice(w.xr ? w.xr->device() : app.makeDevice());
    if (wantHier)
        createHierarchyWorld(w);
    else if (wantHdri)
        createHdriWorld(w);
    else if (wantShadow)
        createShadowTestWorld(w);
    else
        createDemoWorld(w);
    if (w.xr) {
        w.vrCam = spawnVrCamera(w, "vr camera");
        if (w.xr->init(app)) {
            std::vector<std::vector<VkImage>> imgs(w.xr->eyeCount());
            VkExtent2D exts[2];
            for (int i = 0; i < 2; ++i) {
                exts[i] = w.xr->extent(i < w.xr->eyeCount() ? i : 0);
                if (i < w.xr->eyeCount())
                    imgs[i] = w.xr->images(i);
            }
            w.renderer.initXrTarget(app, w.xr->format(), w.xr->eyeCount(), imgs, exts);
        } else
            std::printf(
                "3dsim: no OpenXR session, using virtual head (LMB drag = look, WASD/QE = move, shift = fast)\n");
    }
    auto makeVP = [&w](VkExtent2D ext) {
        return cameraViewProj(w, w.cam, (float)ext.width / (float)ext.height);
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

    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | R reset | S dump shadow map | VR auto-detected, "
                "--no-vr for plain window | esc/close to quit\n");

    auto lastFrame = std::chrono::steady_clock::now();
    bool dumpWasDown = false;
    int frame = 0;
    float fpsAccum = 0.0f;
    int fpsFrames = 0;
    float fps = 0.0f;
    while (!app.windowShouldClose()) {
        const auto nowFrame = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(nowFrame - lastFrame).count();
        lastFrame = nowFrame;
        fpsAccum += dt;
        ++fpsFrames;
        if (fpsAccum >= 0.5f) {
            fps = (float)fpsFrames / fpsAccum;
            fpsAccum = 0.0f;
            fpsFrames = 0;
        }
        if (app.keyIsDown(GLFW_KEY_R))
            resetSofts(w);
        stepPinHolds(w, dt);

        w.rigid.setVelocitySteps(w.ui.joltIters);
        stepRigid(w);
        stepAnimation(w, dt);

        app.pollEvents();
        if (w.xr)
            w.xrFrame = w.xr->poll();
        stepFlyCamera(w, dt);
        if (w.xr)
            stepXr(w);

        resolveWorldTransforms(w);
        syncColliders(w);
        VkCommandBuffer simCmd = app.beginCommands();
        stepSoft(w, simCmd);
        app.submit(simCmd);

        stepStaticRigid(w);
        w.renderer.setViewProj(makeVP(app.extent()), w.reg.get<Transform>(w.cam).pos);
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (w.drawUi)
            w.drawUi(w);
        {
            const VkExtent2D ext = app.extent();
            ImGui::SetNextWindowPos(ImVec2((float)ext.width - 120.0f, 8.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(112.0f, 26.0f), ImGuiCond_Always);
            ImGui::Begin("##fps", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f fps", fps);
            ImGui::TextUnformatted(buf);
            ImGui::End();
        }
        ImGui::Render();

        const uint32_t idx = app.acquireNextImage();
        VkCommandBuffer cmd = app.beginCommands();
        if (w.xr) {
            syncSceneToRenderer(w);
            uint32_t imgIdx[2] = {0, 0};
            if (w.xrFrame.shouldRender)
                for (int i = 0; i < w.xr->eyeCount(); ++i) {
                    w.xr->acquireImage(i);
                    imgIdx[i] = (uint32_t)w.xr->imageIndex(i);
                }
            w.renderer.drawVr(cmd, app, idx, w.ui.bgColor, ImGui::GetDrawData(), w.ui.exposure,
                              w.xrFrame.shouldRender ? imgIdx : nullptr, w.xrFrame.shouldRender ? w.xr->eyeCount() : 0);
            if (w.xrFrame.shouldRender)
                for (int i = 0; i < w.xr->eyeCount(); ++i)
                    w.xr->releaseImage(i);
            w.xr->endFrame(w.xrFrame.shouldRender);
        } else {
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
        }
        ++frame;
        app.present(idx);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    w.sim.shutdown();
    w.renderer.shutdown();
    if (w.xr)
        w.xr->shutdown();
    app.shutdown();
    return 0;
}
