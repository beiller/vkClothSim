#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "scene.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include "world.hpp"
#include "xr/xr.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>

int main(int argc, char** argv) {
    VkApp app;
    if (!app.initInstance(900, 900, "3dsim"))
        return 1;

    World w;
    w.app = &app;
    bool wantHdri = false, wantShadow = false, wantHier = false, wantNoVr = false;
    std::string scenePath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hdri") == 0)
            wantHdri = true;
        else if (std::strcmp(argv[i], "--shadow") == 0)
            wantShadow = true;
        else if (std::strcmp(argv[i], "--hier") == 0)
            wantHier = true;
        else if (std::strcmp(argv[i], "--no-vr") == 0)
            wantNoVr = true;
        else if (std::strcmp(argv[i], "--scene") == 0)
            scenePath = argv[++i];
    }
    // VR: auto-detect the OpenXR runtime (creates the Vulkan device through it, so this must
    // run before the scene); no runtime/HMD -> plain window mode. --no-vr skips detection.
    if (!wantNoVr) {
        w.xr = std::make_unique<Xr>();
        if (!w.xr->createDevice(app))
            w.xr.reset();
    }
    app.initDevice(w.xr ? w.xr->device() : app.makeDevice());
    if (scenePath.empty()) {
        if (wantHdri)
            scenePath = "assets/scenes/hdri.json";
        else if (wantShadow)
            scenePath = "assets/scenes/shadow.json";
        else if (wantHier)
            scenePath = "assets/scenes/tree.json";
        else
            scenePath = "assets/scenes/demo.json";
    }
    createSceneWorld(w, scenePath);
    spawnFpsHud(w);
    if (w.xr) {
        if (w.xr->init(app)) {
            std::vector<std::vector<VkImage>> imgs(w.xr->eyeCount());
            VkExtent2D exts[2];
            for (int i = 0; i < 2; ++i) {
                exts[i] = w.xr->extent(i < w.xr->eyeCount() ? i : 0);
                if (i < w.xr->eyeCount())
                    imgs[i] = w.xr->images(i);
            }
            w.renderer.initXrTarget(app, w.xr->format(), w.xr->eyeCount(), imgs, exts);
            spawnVrRoot(w);
            spawnControllers(w);
        } else
            std::printf(
                "3dsim: no OpenXR session, using virtual head (LMB drag = look, WASD/QE = move, shift = fast)\n");
    }
    spawnInputEntities(w);

    initImGui(app);

    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | scenes in assets/scenes (json + lua) | "
                "VR auto-detected, --no-vr for plain window | --scene file.json | esc/close to quit\n");

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

        app.pollEvents();
        if (w.xr)
            w.xrFrame = w.xr->poll();
        stepMouse(w);
        stepKeyboard(w);
        if (w.xr)
            stepXr(w);
        if (w.xr)
            stepControllers(w);
        // activate any script entities spawned since last frame (fixpoint), then run _animate
        w.script.activateScripts();
        w.script.animateAll(dt);

        // sim runs at a fixed kFrameDt rate, decoupled from the render rate; animation and
        // cameras step with the graphics
        simAccum += dt;
        if (simAccum > 0.1f)
            simAccum = 0.1f;
        while (simAccum >= kFrameDt) {
            w.script.physicsAll(kFrameDt);
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

        const entt::entity cam = findActiveCamera(w); // current camera (swapped by display mode)
        if (cam != entt::null)
            w.renderer.setViewProj(cameraViewProj(w, cam, (float)app.extent().width / (float)app.extent().height),
                                   worldTransform(w.reg, cam).pos);
        imguiNewFrame();
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

    shutdownImGui();
    w.sim.shutdown();
    w.script.shutdown();
    w.renderer.shutdown();
    if (w.xr)
        w.xr->shutdown();
    app.shutdown();
    return 0;
}
