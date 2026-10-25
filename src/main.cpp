#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "scene.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include "engine.hpp"
#include "xr/xr.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <imgui.h>

static float msBetween(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<float, std::milli>(b - a).count();
}

struct PhaseWatch {
    float poll = 0.0f, wait = 0.0f, cpu = 0.0f, jolt = 0.0f, skel = 0.0f, sim = 0.0f,
          rend = 0.0f, xend = 0.0f, tail = 0.0f;
    void reset() { poll = wait = cpu = jolt = skel = sim = rend = xend = tail = 0.0f; }
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
    int winW = 900, winH = 900;
    if (const char* sz = std::getenv("VKSIM_SIZE")) {
        int w, h;
        if (std::sscanf(sz, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            winW = w;
            winH = h;
        }
    }
    if (!app.initInstance(winW, winH, "3dsim"))
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

    uiInit({app.instance(), app.pdev(), app.device(), app.queueFamily(), app.queue(), (int)app.imageCount(),
            [&engine](entt::entity c) -> VkImage { return engine.renderer.gpuTextureImage(c); }});

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
    int softTick = 0;
    const bool uncapped = std::getenv("NO_VSYNC") != nullptr;
    const bool prof = std::getenv("VKSIM_PROFILE") != nullptr;
    if (prof)
        engine.renderer.profOn = true;
    float uiAccum = 0.0f, acqAccum = 0.0f, drawAccum = 0.0f;
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
            engine.phase = {(phaseMs.poll - phaseMs.wait) / fpsFrames, phaseMs.cpu / fpsFrames,
                       phaseMs.jolt / fpsFrames, phaseMs.skel / fpsFrames, phaseMs.sim / fpsFrames,
                       phaseMs.rend / fpsFrames, phaseMs.xend / fpsFrames,
                       (phaseMs.wait + phaseMs.tail) / fpsFrames};
            if (prof) {
                auto& p = engine.renderer.prof;
                std::printf("prof: %5.1ffps poll %5.2f cpu %5.2f jolt %5.2f skel %5.2f sim %5.2f "
                           "ui %5.2f acq %5.2f draw %5.2f (ui %5.2f shadow %5.2f scene %5.2f tonemap %5.2f) "
                           "idle %5.2f\n",
                           fps, engine.phase.poll, engine.phase.cpu, engine.phase.jolt, engine.phase.skel,
                           engine.phase.sim, uiAccum / fpsFrames, acqAccum / fpsFrames, drawAccum / fpsFrames,
                           p.ui / fpsFrames, p.shadow / fpsFrames, p.scene / fpsFrames, p.tonemap / fpsFrames,
                           engine.phase.idle);
                p.reset();
            }
            uiAccum = acqAccum = drawAccum = 0.0f;
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

        float frameJolt = 0.0f, frameSkel = 0.0f, frameSoft = 0.0f, frameAcq = 0.0f;
        simAccum += dt;
        if (simAccum > 0.4f)
            simAccum = 0.4f;
        while (simAccum >= kFrameDt) {
            engine.script.physicsAll(kFrameDt);
            const auto tJ = std::chrono::steady_clock::now();
            stepRigid(engine.rigid, engine.reg);
            frameJolt += msBetween(tJ, std::chrono::steady_clock::now());
            resolveWorldTransforms(engine.reg);
            // skel: skin CPU + its own blocking submit, so the GPU skin time is its own bucket
            const auto tS = std::chrono::steady_clock::now();
            {
                VkCommandBuffer skelCmd = app.beginCommands();
                engine.gpuSim.skin.step(engine.reg, skelCmd);
                app.submit(skelCmd);
            }
            frameSkel += msBetween(tS, std::chrono::steady_clock::now());
            // sim: cloth only, its own submit, gated to kSoftHz (not the full rigid rate)
            if ((softTick++ % kSoftSubdiv) == 0) {
                const auto tC = std::chrono::steady_clock::now();
                VkCommandBuffer softCmd = app.beginCommands();
                stepSoft(engine, softCmd);
                app.submit(softCmd);
                frameSoft += msBetween(tC, std::chrono::steady_clock::now());
            }
            simAccum -= kFrameDt;
        }
        // Pace the loop to the sim rate when uncapped (NO_VSYNC): otherwise the
        // render spins at full speed and the sim runs at kRigidHz only in the
        // average sense. Under vsync (default) this is a no-op — vsync paces it.
        if (uncapped && simAccum < kFrameDt)
            std::this_thread::sleep_for(std::chrono::duration<double>(kFrameDt - simAccum));
        resolveWorldTransforms(engine.reg);
        const auto tSim = std::chrono::steady_clock::now();

        const entt::entity cam = findActiveCamera(engine.reg);
        if (cam != entt::null)
            engine.renderer.setViewProj(cameraView(engine.reg, cam, (float)app.extent().width / (float)app.extent().height));
        // UI pointer: world-space ray (VR: controller aim; window: camera through cursor)
        // -> rigid-body raycast against each canvas collider -> uv of the hit point
        std::vector<entt::entity> canvases;
        for (auto e : engine.reg.view<UICanvas>())
            canvases.push_back(e);
        auto canvasPx = [&](entt::entity c) -> V2 {
            for (auto [e, w] : engine.reg.view<UIWindow>().each())
                if (w.canvas == c)
                    return w.size;
            return V2{320.0f, 200.0f};
        };
        auto tryRay = [&](entt::entity canvas, const V3& o, const V3& d, UiFeed& f) {
            // the hand's own bodies (anchor, controller) hug the ray origin and would win at t=0;
            // exclude them by name so the canvas stays castable even when the hand is close
            std::vector<entt::entity> excl;
            engine.reg.view<Name>().each([&](entt::entity e, const Name& n) {
                if (std::strstr(n.id.c_str(), "hand") || std::strstr(n.id.c_str(), "controller"))
                    excl.push_back(e);
            });
            const RayHit h = engine.rigid.raycast(o, d, 30.0f, excl);
            if (!h.hit || engine.rigid.entityFor(h.body) != canvas)
                return false;
            V2 uv;
            if (!uiPanelUv(engine.reg, canvas, h.pos, uv))
                return false;
            const V2 px = canvasPx(canvas);
            f.pos = {(float)uv.x * px.x, (1.0f - (float)uv.y) * px.y};
            return true;
        };
        std::vector<UiFeed> feeds;
        for (entt::entity c : canvases) {
            engine.renderer.uiEnsureTarget(engine.reg, c);
            UiFeed f{c, ImVec2{-1.0f, -1.0f}, false, 0.0f, false};
            if (engine.xr && fr.running && fr.havePose) {
                const entt::entity root = findMarker<VrRoot>(engine.reg);
                const V3 rootPos = root != entt::null ? worldTransform(engine.reg, root).pos : V3{0, 0, 0};
                for (int i = 0; i < 2; ++i) {
                    const XrController& ctl = fr.controller[i];
                    if (!ctl.active)
                        continue;
                    const V3 o = rootPos + ctl.aimPos; // the aim pose is in the vr-root (locomotion) frame
                    const V3 d = quatFrom(ctl.aimQuat) * V3{0, 0, -1};
                    f.down = ctl.buttons[0]; // A = click; hover below
                    f.wheel = -ctl.axis[1];
                    if (tryRay(c, o, d, f))
                        break;
                }
            } else if (cam != entt::null) {
                const CameraView cv = cameraView(engine.reg, cam, (float)app.extent().width / (float)app.extent().height);
                double mx, my;
                glfwGetCursorPos(app.glfwWindow(), &mx, &my);
                // cursor is in window pixels but the camera projects in framebuffer pixels
                const float x = 2.0f * (float)mx / (float)app.extent().width - 1.0f;
                const float y = 1.0f - 2.0f * (float)my / (float)app.extent().height;
                const V3 cd = {(x * 0.5f + 0.5f) * (cv.tanR - cv.tanL) + cv.tanL,
                              (y * 0.5f + 0.5f) * (cv.tanU - cv.tanD) + cv.tanD, -1.0f};
                const V3 o = cv.pos;
                // the view matrix columns are the camera's world basis; world dir = camera-space dir expressed in that basis
                const V3 right = cv.view[0];
                const V3 up = cv.view[1];
                const V3 back = cv.view[2];
                const V3 d = glm::normalize(right * cd.x + up * cd.y + back * cd.z);
                f.down = glfwGetMouseButton(app.glfwWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                tryRay(c, o, d, f);
            }
            feeds.push_back(f);
        }
        if (!feeds.empty() && engine.showDemo)
            feeds[0].demo = true;
        for (entt::entity c : canvases)
            uiEnsureContext(engine.reg, c, engine.renderer.uiTargetPass(c), engine.renderer.uiTargetFb(c));
        uiFrame(engine.reg, feeds);

        uiAccum += msBetween(tSim, std::chrono::steady_clock::now());
        const auto tAcq = std::chrono::steady_clock::now();
        const uint32_t idx = app.acquireNextImage();
        frameAcq += msBetween(tAcq, std::chrono::steady_clock::now());
        acqAccum += frameAcq;
        VkCommandBuffer cmd = app.beginCommands();
        if (engine.xr) {
            uint32_t imgIdx[2] = {0, 0};
            if (fr.shouldRender)
                for (int i = 0; i < engine.xr->eyeCount(); ++i) {
                    engine.xr->acquireImage(i);
                    imgIdx[i] = (uint32_t)engine.xr->imageIndex(i);
                }
            engine.renderer.drawVr(cmd, app, idx, engine.reg, engine.render, cam,
                                  fr.shouldRender ? imgIdx : nullptr, fr.shouldRender ? engine.xr->eyeCount() : 0);
            if (fr.shouldRender)
                for (int i = 0; i < engine.xr->eyeCount(); ++i)
                    engine.xr->releaseImage(i);
        } else {
            const auto tDraw = std::chrono::steady_clock::now();
            engine.renderer.draw(cmd, app, idx, engine.reg, engine.render);
            drawAccum += msBetween(tDraw, std::chrono::steady_clock::now());
        }
        const auto tRender = std::chrono::steady_clock::now();
        if (engine.xr)
            engine.xr->endFrame(fr.shouldRender);
        const auto tXrEnd = std::chrono::steady_clock::now();
        app.present(idx, !engine.xr);
        phaseMs.poll += msBetween(nowFrame, tPoll);
        phaseMs.wait += wait;
        phaseMs.cpu += msBetween(tPoll, tSim) - frameJolt - frameSkel - frameSoft;
        phaseMs.jolt += frameJolt;
        phaseMs.skel += frameSkel;
        phaseMs.sim += frameSoft;
        // acquireNextImage's fence wait is vsync/pacing, not render work — keep it out of rend
        phaseMs.rend += msBetween(tSim, tRender) - frameAcq;
        phaseMs.xend += msBetween(tRender, tXrEnd);
        phaseMs.tail += msBetween(tXrEnd, std::chrono::steady_clock::now()) + frameAcq;
        if (engine.xr)
            pace.add(msBetween(nowFrame, tXrEnd) - wait);
    }

    uiShutdown();
    engine.gpuSim.shutdown();
    engine.script.shutdown();
    engine.renderer.shutdown();
    if (engine.xr)
        engine.xr->shutdown();
    app.shutdown();
    return rc;
}
