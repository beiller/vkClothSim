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

// per-frame phase intervals (ms); wait is the HMD pacing wait, measured inside poll and
// republished as idle
struct PhaseWatch {
    float poll = 0.0f, wait = 0.0f, sim = 0.0f, rend = 0.0f, xend = 0.0f, tail = 0.0f;
    void reset() { poll = wait = sim = rend = xend = tail = 0.0f; }
};

// VR: real work per frame (excl. the HMD pacing wait); a frame whose work exceeds the HMD
// budget drops a frame; logs 120-frame windows
struct PaceWatch {
    float total = 0.0f, max = 0.0f;
    int frames = 0;
    void add(float work) {
        total += work;
        if (work > max)
            max = work;
        if (++frames % 120 == 0) {
            std::fprintf(stderr, "3dsim: vr work avg %.1f ms, max %.1f ms\n", total / 120, max);
            total = max = 0.0f;
        }
    }
};

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
    initEngine(engine);
    spawnFpsHud(engine);
    if (engine.xr) {
        engine.xrSession = engine.xr->init(app);
        if (engine.xrSession) {
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

    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | startup scene select (or --scene file.json) | "
                "VR auto-detected, --no-vr for plain window | esc/close to quit\n");

    // the menu is a scene (assets/scenes/menu.json); --scene bypasses it; a boot load fail is a hard CLI fail
    const std::string kMenuPath = "assets/scenes/menu.json";
    int rc = 0;
    if (!loadScene(engine, scenePath.empty() ? kMenuPath : scenePath)) {
        std::fprintf(stderr, "3dsim: %s\n", engine.loadError.c_str());
        rc = 1;
    }

    auto lastFrame = std::chrono::steady_clock::now();
    float fpsAccum = 0.0f;
    int fpsFrames = 0;
    float fps = 0.0f;
    float simAccum = 0.0f;
    PhaseWatch phaseMs;
    PaceWatch pace;
    while (rc == 0 && !app.windowShouldClose()) {
        const auto nowFrame = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(nowFrame - lastFrame).count();
        lastFrame = nowFrame;
        fpsAccum += dt;
        ++fpsFrames;
        if (fpsAccum >= 0.5f) {
            fps = (float)fpsFrames / fpsAccum;
            engine.phase = {(phaseMs.poll - phaseMs.wait) / fpsFrames, phaseMs.sim / fpsFrames,
                       phaseMs.rend / fpsFrames, phaseMs.xend / fpsFrames,
                       (phaseMs.wait + phaseMs.tail) / fpsFrames};
            phaseMs.reset();
            fpsAccum = 0.0f;
            fpsFrames = 0;
        }
        engine.fps = fps;

        float wait = 0.0f; // HMD pacing wait (0 in window mode)
        app.pollEvents();
        XrFrame fr; // this frame's headset state (head, eyes, controllers); frame-local, not world state
        if (engine.xr) {
            const auto tWait = std::chrono::steady_clock::now();
            engine.xr->waitFrame();
            wait = msBetween(tWait, std::chrono::steady_clock::now());
            fr = engine.xr->poll();
        }
        // frame start, previous frame's GPU work done (sync present / XR waitFrame): release
        // the resources queued by the last frame's entity destroys
        engine.renderer.flushFrees();
        engine.gpuSim.soft.flushFrees();
        engine.gpuSim.skin.flushFrees();
        // a requested scene switch (scene.request_scene): applied once the click callback has
        // returned (the load resets the script host); a failure falls back to the menu
        if (!engine.pendingScene.empty()) {
            std::string p;
            std::swap(p, engine.pendingScene);
            if (!loadScene(engine, p) && engine.scenePath != kMenuPath)
                loadScene(engine, kMenuPath);
        }
        const auto tPoll = std::chrono::steady_clock::now();
        stepMouse(engine);
        stepKeyboard(engine);
        // activate scripts to fixpoint, then _animate: walk locomotion moves the VR root
        // BEFORE stepXr bakes walkPos into the camera views, so camera and scene share one
        // root pose this frame (a lagged walk step would jitter the perceived hand position)
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
            engine.renderer.setViewProj(cameraView(engine, cam, (float)app.extent().width / (float)app.extent().height));
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
        app.present(idx, !engine.xr);
        phaseMs.poll += msBetween(nowFrame, tPoll); // incl. wait; republished into idle
        phaseMs.wait += wait;
        phaseMs.sim += msBetween(tPoll, tSim);
        phaseMs.rend += msBetween(tSim, tRender);
        phaseMs.xend += msBetween(tRender, tXrEnd);
        phaseMs.tail += msBetween(tXrEnd, std::chrono::steady_clock::now());
        if (engine.xr)
            pace.add(msBetween(nowFrame, tXrEnd) - wait);
    }

    shutdownImGui();
    engine.gpuSim.shutdown(); // frees the skinned groups' pos/nrm the renderer only aliases
    engine.script.shutdown();
    engine.renderer.shutdown();
    if (engine.xr)
        engine.xr->shutdown();
    app.shutdown();
    return rc;
}
