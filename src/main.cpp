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

struct PhaseWatch {
    float poll = 0.0f, wait = 0.0f, sim = 0.0f, rend = 0.0f, xend = 0.0f, tail = 0.0f;
    void reset() { poll = wait = sim = rend = xend = tail = 0.0f; }
};

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
            spawnVrRoot(engine.reg);
            spawnControllers(engine);
        } else
            std::printf(
                "3dsim: no OpenXR session, using virtual head (LMB drag = look, WASD/QE = move, shift = fast)\n");
    }
    spawnInputEntities(engine);

    initImGui(app);

    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | startup scene select (or --scene file.json) | "
                "VR auto-detected, --no-vr for plain window | esc/close to quit\n");

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

        float wait = 0.0f;
        app.pollEvents();
        app.resizeBuffers();
        XrFrame fr;
        if (engine.xr) {
            const auto tWait = std::chrono::steady_clock::now();
            engine.xr->waitFrame();
            wait = msBetween(tWait, std::chrono::steady_clock::now());
            fr = engine.xr->poll();
        }
        engine.renderer.flushFrees();
        engine.gpuSim.soft.flushFrees();
        engine.gpuSim.skin.flushFrees();
        if (!engine.pendingScene.empty()) {
            std::string p;
            std::swap(p, engine.pendingScene);
            if (!loadScene(engine, p) && engine.scenePath != kMenuPath)
                loadScene(engine, kMenuPath);
        }
        const auto tPoll = std::chrono::steady_clock::now();
        stepMouse(engine);
        stepKeyboard(engine);
        engine.script.activateScripts();
        engine.script.animateAll(dt);
        if (engine.xr) {
            stepXr(engine.reg, fr);
            stepControllers(engine.reg, fr);
        }

        simAccum += dt;
        if (simAccum > 0.4f)
            simAccum = 0.4f;
        while (simAccum >= kFrameDt) {
            engine.script.physicsAll(kFrameDt);
            stepRigid(engine.rigid, engine.reg);
            resolveWorldTransforms(engine.reg);
            VkCommandBuffer simCmd = app.beginCommands();
            engine.gpuSim.skin.step(engine.reg, simCmd);
            stepSoft(engine, simCmd);
            app.submit(simCmd);
            simAccum -= kFrameDt;
        }
        resolveWorldTransforms(engine.reg);
        const auto tSim = std::chrono::steady_clock::now();

        const entt::entity cam = findActiveCamera(engine.reg);
        if (cam != entt::null)
            engine.renderer.setViewProj(cameraView(engine.reg, cam, (float)app.extent().width / (float)app.extent().height));
        imguiNewFrame();
        drawUi(engine);
        if (engine.showDemo)
            ImGui::ShowDemoWindow(&engine.showDemo);
        ImGui::Render();

        Renderer::SkinnedMeshProvider skinned;
        skinned.meshGpu = [&](entt::entity m, MeshGpu& rw) { return engine.gpuSim.skin.meshGpu(m, rw); };
        engine.renderer.adoptSkinned(engine.reg, skinned);
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
        phaseMs.poll += msBetween(nowFrame, tPoll);
        phaseMs.wait += wait;
        phaseMs.sim += msBetween(tPoll, tSim);
        phaseMs.rend += msBetween(tSim, tRender);
        phaseMs.xend += msBetween(tRender, tXrEnd);
        phaseMs.tail += msBetween(tXrEnd, std::chrono::steady_clock::now());
        if (engine.xr)
            pace.add(msBetween(nowFrame, tXrEnd) - wait);
    }

    shutdownImGui();
    engine.gpuSim.shutdown();
    engine.script.shutdown();
    engine.renderer.shutdown();
    if (engine.xr)
        engine.xr->shutdown();
    app.shutdown();
    return rc;
}
