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
    bool wantHdri = false, wantShadow = false, wantHier = false, wantNoVr = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hdri") == 0)
            wantHdri = true;
        else if (std::strcmp(argv[i], "--shadow") == 0)
            wantShadow = true;
        else if (std::strcmp(argv[i], "--hier") == 0)
            wantHier = true;
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
    // app-level fps HUD, also a UI window entity
    const entt::entity fpsWin = spawnUiWindow(w,
                                              UIWindow{"##fps",
                                                       [&w] {
                                                           const VkExtent2D ext = w.app->extent();
                                                           return V2{(float)ext.width - 120.0f, 8.0f};
                                                       },
                                                       V2{112.0f, 26.0f},
                                                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                           ImGuiWindowFlags_NoSavedSettings |
                                                           ImGuiWindowFlags_NoBringToFrontOnFocus});
    spawnWidget(w, fpsWin,
                UIText{"",
                        [&w] {
                            char buf[32];
                            std::snprintf(buf, sizeof(buf), "%.0f fps", w.fps);
                            return std::string(buf);
                        }});
    if (w.xr) {
        spawnVrRig(w, findActiveCamera(w));
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
    const entt::entity cam = findActiveCamera(w);
    auto makeVP = [cam, &w](VkExtent2D ext) {
        return cameraViewProj(w, cam, (float)ext.width / (float)ext.height);
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

    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | R reset | VR auto-detected, "
                "--no-vr for plain window | esc/close to quit\n");

    auto lastFrame = std::chrono::steady_clock::now();
    float fpsAccum = 0.0f;
    int fpsFrames = 0;
    float fps = 0.0f;
    float simAccum = 0.0f;
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
        w.fps = fps;
        if (app.keyIsDown(GLFW_KEY_R))
            resetSofts(w);
        stepPinHolds(w, dt);

        app.pollEvents();
        if (w.xr)
            w.xrFrame = w.xr->poll();
        stepFlyCamera(w, dt);
        if (w.xr)
            stepXr(w);
        stepAnimation(w, dt);

        // sim runs at a fixed kFrameDt rate, decoupled from the render rate; animation and
        // cameras step with the graphics
        simAccum += dt;
        if (simAccum > 0.1f)
            simAccum = 0.1f;
        while (simAccum >= kFrameDt) {
            stepRigid(w);
            resolveWorldTransforms(w);
            syncColliders(w);
            VkCommandBuffer simCmd = app.beginCommands();
            stepSoft(w, simCmd);
            app.submit(simCmd);
            simAccum -= kFrameDt;
        }
        // 0-step frames still need fresh world transforms for the renderer (camera, VR head,
        // animation)
        resolveWorldTransforms(w);

        stepStaticRigid(w);
        w.renderer.setViewProj(makeVP(app.extent()), worldTransform(w.reg, cam).pos);
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawUi(w);
        if (w.showDemo) // ImGui's own demo window, not ours to decompose
            ImGui::ShowDemoWindow(&w.showDemo);
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
            w.renderer.drawVr(cmd, app, idx, w.render.bgColor, ImGui::GetDrawData(), w.render.exposure,
                              w.xrFrame.shouldRender ? imgIdx : nullptr, w.xrFrame.shouldRender ? w.xr->eyeCount() : 0);
            if (w.xrFrame.shouldRender)
                for (int i = 0; i < w.xr->eyeCount(); ++i)
                    w.xr->releaseImage(i);
            w.xr->endFrame(w.xrFrame.shouldRender);
        } else {
            draw(w, cmd, idx, ImGui::GetDrawData());
        }
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
