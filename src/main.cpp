#include <vulkan/vulkan.h>

#include "scene.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include "engine.hpp"
#include "xr/xr.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <imgui.h>

static float msBetween(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<float, std::milli>(b - a).count();
}

int main(int argc, char** argv) {
    VkApp app;
    if (!app.initInstance(900, 900, "3dsim"))
        return 1;

    Engine engine;
    engine.app = &app;
    bool wantNoVr = false;
    std::string scenePath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-vr") == 0)
            wantNoVr = true;
        else if (std::strcmp(argv[i], "--scene") == 0)
            scenePath = argv[++i];
    }
    // VR: auto-detect the OpenXR runtime (creates the Vulkan device through it, so this must
    // run before the scene); no runtime/HMD -> plain window mode. --no-vr skips detection.
    if (!wantNoVr) {
        engine.xr = std::make_unique<Xr>();
        if (!engine.xr->createDevice(app))
            engine.xr.reset();
    }
    app.initDevice(engine.xr ? engine.xr->device() : app.makeDevice());
    if (scenePath.empty())
        scenePath = "assets/scenes/demo.json";
    createScene(engine, scenePath);
    spawnFpsHud(engine);
    if (engine.xr) {
        if (engine.xr->init(app)) {
            std::vector<std::vector<VkImage>> imgs(engine.xr->eyeCount());
            VkExtent2D exts[2];
            for (int i = 0; i < 2; ++i) {
                exts[i] = engine.xr->extent(i < engine.xr->eyeCount() ? i : 0);
                if (i < engine.xr->eyeCount())
                    imgs[i] = engine.xr->images(i);
            }
            engine.renderer.initXrTarget(app, engine.xr->format(), engine.xr->eyeCount(), imgs, exts);
            spawnVrRoot(engine);
            spawnControllers(engine);
        } else
            std::printf(
                "3dsim: no OpenXR session, using virtual head (LMB drag = look, WASD/QE = move, shift = fast)\n");
    }
    spawnInputEntities(engine);

    initImGui(app);

    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | scenes in assets/scenes (json + lua) | "
                "VR auto-detected, --no-vr for plain window | --scene file.json | esc/close to quit\n");

    auto lastFrame = std::chrono::steady_clock::now();
    float fpsAccum = 0.0f;
    int fpsFrames = 0;
    float fps = 0.0f;
    float simAccum = 0.0f;
    float msPoll = 0.0f, msSim = 0.0f, msRender = 0.0f, msXrEnd = 0.0f, msIdle = 0.0f;
    while (!app.windowShouldClose()) {
        const auto nowFrame = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(nowFrame - lastFrame).count();
        lastFrame = nowFrame;
        fpsAccum += dt;
        ++fpsFrames;
        if (fpsAccum >= 0.5f) {
            fps = (float)fpsFrames / fpsAccum;
            engine.phase = {msPoll / fpsFrames, msSim / fpsFrames, msRender / fpsFrames, msXrEnd / fpsFrames,
                       msIdle / fpsFrames};
            msPoll = msSim = msRender = msXrEnd = msIdle = 0.0f;
            fpsAccum = 0.0f;
            fpsFrames = 0;
        }
        engine.fps = fps;

        app.pollEvents();
        XrFrame fr; // this frame's headset state (head, eyes, controllers); frame-local, not world state
        if (engine.xr)
            fr = engine.xr->poll();
        // frame start, previous frame's GPU work done (sync present / XR waitFrame): release
        // the resources queued by the last frame's entity destroys
        engine.renderer.flushFrees();
        engine.gpuSim.soft.flushFrees();
        engine.gpuSim.skin.flushFrees();
        const auto tPoll = std::chrono::steady_clock::now();
        stepMouse(engine);
        stepKeyboard(engine);
        // activate scripts to fixpoint, then _animate: walk locomotion moves the VR root
        // BEFORE stepXr bakes walkPos into the camera views, so camera and scene share one root
        // pose this frame (a lagged walk step would jitter the perceived hand position)
        engine.script.activateScripts();
        engine.script.animateAll(dt);
        if (engine.xr) {
            stepXr(engine, fr);
            stepControllers(engine, fr);
        }

        // sim runs at a fixed kFrameDt rate, decoupled from the render rate; animation and
        // cameras step with the graphics
        simAccum += dt;
        if (simAccum > 0.1f)
            simAccum = 0.1f;
        while (simAccum >= kFrameDt) {
            engine.script.physicsAll(kFrameDt);
            stepRigid(engine);
            resolveWorldTransforms(engine);
            VkCommandBuffer simCmd = app.beginCommands();
            engine.gpuSim.skin.step(engine.reg, simCmd); // GPU skin before the cloth sim (cloth-on-skeleton hook)
            stepSoft(engine, simCmd);
            app.submit(simCmd);
            simAccum -= kFrameDt;
        }
        // 0-step frames still need fresh world transforms for the renderer (camera, VR head,
        // animation)
        resolveWorldTransforms(engine);
        const auto tSim = std::chrono::steady_clock::now();

        const entt::entity cam = findActiveCamera(engine); // current camera (swapped by display mode)
        if (cam != entt::null)
            engine.renderer.setViewProj(cameraViewProj(engine, cam, (float)app.extent().width / (float)app.extent().height),
                                   worldTransform(engine.reg, cam).pos);
        imguiNewFrame();
        drawUi(engine);
        if (engine.showDemo) // ImGui's own demo window, not ours to decompose
            ImGui::ShowDemoWindow(&engine.showDemo);
        ImGui::Render();

        const uint32_t idx = app.acquireNextImage();
        VkCommandBuffer cmd = app.beginCommands();
        if (engine.xr) {
            uint32_t imgIdx[2] = {0, 0};
            if (fr.shouldRender)
                for (int i = 0; i < engine.xr->eyeCount(); ++i) {
                    engine.xr->acquireImage(i);
                    imgIdx[i] = (uint32_t)engine.xr->imageIndex(i);
                }
            engine.renderer.drawVr(cmd, app, idx, engine.reg, engine.render, ImGui::GetDrawData(), cam,
                              fr.shouldRender ? imgIdx : nullptr, fr.shouldRender ? engine.xr->eyeCount() : 0);
            if (fr.shouldRender)
                for (int i = 0; i < engine.xr->eyeCount(); ++i)
                    engine.xr->releaseImage(i);
        } else {
            draw(engine, cmd, idx, ImGui::GetDrawData());
        }
        const auto tRender = std::chrono::steady_clock::now();
        if (engine.xr)
            engine.xr->endFrame(fr.shouldRender);
        const auto tXrEnd = std::chrono::steady_clock::now();
        app.present(idx,
                    !engine.xr); // window: sync present (vsync wait shows in 'idle'); VR: compositor paces via waitFrame
        msPoll += msBetween(nowFrame, tPoll);
        msSim += msBetween(tPoll, tSim);
        msRender += msBetween(tSim, tRender);
        msXrEnd += msBetween(tRender, tXrEnd);
        msIdle += msBetween(tXrEnd, std::chrono::steady_clock::now());
    }

    shutdownImGui();
    engine.gpuSim.shutdown(); // frees the skinned groups' pos/nrm the renderer only aliases
    engine.script.shutdown();
    engine.renderer.shutdown();
    if (engine.xr)
        engine.xr->shutdown();
    app.shutdown();
    return 0;
}
