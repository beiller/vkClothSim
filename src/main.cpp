#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "app/engine.hpp"
#include "assets.hpp"
#include "scene.hpp"
#include "systems.hpp"
#include "util.hpp"
#include "vk/vkapp.hpp"
#include "xr/xr.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <imgui.h>

struct PhaseWatch {
    float poll = 0.0f, wait = 0.0f, cpu = 0.0f, jolt = 0.0f, skel = 0.0f, sim = 0.0f, rend = 0.0f, xend = 0.0f,
          tail = 0.0f;
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
            // too much output noise!
            //std::fprintf(stderr, "3dsim: vr work avg %.1f ms, max %.1f ms\n", total / 120, max);
            total = max = 0.0f;
        }
    }
};

// Refresh period (ms) of the display driving the window, for vsync-aligned frame pacing
double vsyncPeriodMs(GLFWwindow* win) {
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (win) {
        int wx = 0, wy = 0;
        glfwGetWindowPos(win, &wx, &wy);
        int ww = 0, wh = 0;
        glfwGetWindowSize(win, &ww, &wh);
        const int cx = wx + ww / 2, cy = wy + wh / 2; // monitor under the window center
        int n = 0;
        for (GLFWmonitor** m = glfwGetMonitors(&n); m != nullptr && *m != nullptr; ++m) {
            const GLFWvidmode* vm = glfwGetVideoMode(*m);
            if (!vm)
                continue;
            int mx = 0, my = 0;
            glfwGetMonitorPos(*m, &mx, &my);
            if (cx >= mx && cx < mx + vm->width && cy >= my && cy < my + vm->height) {
                mon = *m;
                break;
            }
        }
    }
    const GLFWvidmode* vm = glfwGetVideoMode(mon);
    return (vm && vm->refreshRate > 0) ? 1000.0 / vm->refreshRate : 16.667;
}

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
    bool wantNoVr = false, noGraphics = false, headless = false, noInterp = false, syncSubmit = false;
    int maxFrames = -1;
    int physHz = kPhysHzDefault;
    bool cliPhysHz = false;
    std::string scenePath;
    bool scenesDirSet = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-vr") == 0)
            wantNoVr = true;
        else if (std::strcmp(argv[i], "--no-graphics") == 0)
            noGraphics = true;
        else if (std::strcmp(argv[i], "--headless") == 0)
            headless = true;
        else if (std::strcmp(argv[i], "--frames") == 0)
            maxFrames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--phys-hz") == 0) {
            physHz = std::atoi(argv[++i]);
            cliPhysHz = true;
        }
        else if (std::strcmp(argv[i], "--no-interp") == 0)
            noInterp = true; // render the latest tick state directly: no interpolation latency
        else if (std::strcmp(argv[i], "--sync") == 0)
            syncSubmit = true; // legacy blocking GPU submit (bisection for the async pipeline, FRAME_PACING.md p6)
        else if (std::strcmp(argv[i], "--scene") == 0)
            scenePath = argv[++i];
        else if (std::strcmp(argv[i], "--scenes-dir") == 0) {
            setScenesDir(argv[++i]);
            scenesDirSet = true;
        }
    }
    if (!scenesDirSet && !std::filesystem::exists("assets/scenes"))
        setScenesDir(exeDir() + "/../assets/scenes");
    if (!scenePath.empty() && !std::filesystem::exists(scenePath)) {
        scenePath = scenesDir() + "/" + scenePath;
        if (!std::filesystem::exists(scenePath) && std::filesystem::exists(scenePath + ".json"))
            scenePath += ".json";
    }
    if (noGraphics && headless) {
        std::fprintf(stderr, "3dsim: --no-graphics and --headless conflict\n");
        return 1;
    }
    if (!noGraphics && (headless ? !app.initHeadless() : !app.initInstance(winW, winH, "3dsim")))
        return 1;

    Engine engine;
    engine.app = noGraphics ? nullptr : &app;
    engine.hasDevice = !noGraphics;
    engine.hasWindow = !noGraphics && !headless;
    if (engine.hasWindow && !wantNoVr) {
        engine.xr = std::make_unique<Xr>();
        if (!engine.xr->createDevice(app))
            engine.xr.reset();
    }
    if (engine.hasWindow)
        app.initDevice(engine.xr ? engine.xr->device() : app.makeDevice());
    app.setAsync(!syncSubmit); // async pipeline by default; --sync restores the legacy blocking submit
    initEngine(engine);
    engine.physics.setPhysicsHook([&engine](float sdt) { engine.script.physicsAll(sdt); });
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
            engine.graphics.initXrTarget(app, engine.xr->format(), engine.xr->eyeCount(), imgs, exts);
            spawnVrRoot(engine.reg);
            spawnControllers(engine);
        } else
            std::printf(
                "3dsim: no OpenXR session, using virtual head (LMB drag = look, WASD/QE = move, shift = fast)\n");
    }
    spawnInputEntities(engine);

    if (engine.hasWindow)
        uiInit({app.instance(), app.pdev(), app.device(), app.queueFamily(), app.queue(), (int)app.imageCount(),
                [&engine](entt::entity c) -> VkImage { return engine.graphics.canvasImage(c); }});
    uiSetButtonHook([&engine](const std::string& u, entt::entity b) { engine.script.callUrl(u, b); });
    uiSetChangeHook([&engine](const std::string& u, entt::entity w) { engine.script.callUrl(u, w); });

    if (noGraphics)
        std::printf("3dsim: CPU-only run (no graphics) | physics + scripts | --frames N to exit\n");
    else if (headless)
        std::printf("3dsim: headless GPU compute (no window) | physics + soft/skin | --frames N to exit\n");
    else
        std::printf("3dsim: vulkan+imgui | GPU soft-body sim | startup scene select (or --scene file.json) | "
                    "VR auto-detected, --no-vr for plain window | esc/close to quit\n");

    const std::string kMenuPath = scenesDir() + "/menu.json";
    int rc = 0;
    if (!loadScene(engine, scenePath.empty() ? kMenuPath : scenePath)) {
        std::fprintf(stderr, "3dsim: %s\n", engine.loadError.c_str());
        rc = 1;
    }

    auto lastFrame = std::chrono::steady_clock::now();
    // Fixed-timestep accumulator (integer ns; see FRAME_PACING.md): physics ticks at physHz
    // regardless of display rate, rendering runs at display rate. Rate comes from the scene's
    // PhysicsSettings.physHz unless overridden by CLI --phys-hz. Resolved per frame so a scene
    // can change it live (e.g. via a UI slider bound to settings.PhysicsSettings.physHz).
    int64_t tickNs = 0;
    float tickDt = 0.0f;
    int maxSteps = 1;
    int physHzCur = -1;
    auto applyPhysHz = [&](int hz) {
        if (hz < 1 || hz > 1000)
            hz = kPhysHzDefault;
        if (hz == physHzCur)
            return;
        physHzCur = hz;
        tickNs = 1'000'000'000 / hz;
        tickDt = 1.0f / (float)hz;
        maxSteps = std::clamp((int)(kMaxSimPerFrameNs / tickNs), 1, 8);
    };
    int64_t acc = 0;
    // vsync pacing state: nextEdge is the upcoming display refresh edge we present on
    auto paceEdge = std::chrono::steady_clock::now();
    const auto pacePeriod = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double, std::milli>(vsyncPeriodMs(app.glfwWindow())));
    float fpsAccum = 0.0f;
    int fpsFrames = 0;
    int64_t tickAccum = 0; // fixed-tick count, for the profile line (avg ticks/frame)
    float fps = 0.0f;
    const bool prof = std::getenv("VKSIM_PROFILE") != nullptr;
    if (prof && engine.hasDevice)
        engine.graphics.profOn() = true;
    float uiAccum = 0.0f, acqAccum = 0.0f, drawAccum = 0.0f;
    PhaseWatch phaseMs;
    PaceWatch pace;
    int frameCount = 0;
    while (rc == 0) {
        if (maxFrames >= 0 && ++frameCount > maxFrames)
            break;
        if (engine.hasWindow && app.windowShouldClose())
            break;
        const auto nowFrame = std::chrono::steady_clock::now();
        // Frame time in integer ns, clamped so a hitch slows the sim down instead of making it jump.
        const int64_t frameNs = engine.hasWindow
            ? std::min<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(nowFrame - lastFrame).count(),
                                kMaxFrameNs)
            : kHeadlessFrameNs;
        const float dt = (float)frameNs * 1e-9f;
        // Render-rate hook dt: same clamp as the old per-frame simDt, so _animate scripts see no change.
        const float frameDt = std::min(dt, kMaxPhysDt);
        lastFrame = nowFrame;
        acc += frameNs;
        fpsAccum += dt;
        ++fpsFrames;
        if (fpsAccum >= 0.5f) {
            fps = (float)fpsFrames / fpsAccum;
            const float fframes = (float)fpsFrames;
            engine.phase = {(phaseMs.poll - phaseMs.wait) / fframes,
                            phaseMs.cpu / fframes,
                            phaseMs.jolt / fframes,
                            phaseMs.skel / fframes,
                            phaseMs.sim / fframes,
                            phaseMs.rend / fframes,
                            phaseMs.xend / fframes,
                            (phaseMs.wait + phaseMs.tail) / fframes};
            if (prof) {
                auto& p = engine.graphics.prof();
                std::printf("prof: %5.1ffps poll %5.2f cpu %5.2f jolt %5.2f skel %5.2f sim %5.2f "
                            "ticks %4.2f ui %5.2f acq %5.2f draw %5.2f (ui %5.2f shadow %5.2f scene %5.2f tonemap %5.2f) "
                            "idle %5.2f\n",
                            fps, engine.phase.poll, engine.phase.cpu, engine.phase.jolt, engine.phase.skel,
                            engine.phase.sim, (double)tickAccum / fpsFrames, uiAccum / fpsFrames, acqAccum / fpsFrames,
                            drawAccum / fpsFrames, p.ui / fpsFrames, p.shadow / fpsFrames, p.scene / fpsFrames,
                            p.tonemap / fpsFrames, engine.phase.idle);
                p.reset();
            }
            uiAccum = acqAccum = drawAccum = 0.0f;
            phaseMs.reset();
            fpsAccum = 0.0f;
            fpsFrames = 0;
            tickAccum = 0;
        }
        engine.fps = fps;
        writePerfText(engine);

        float wait = 0.0f;
        XrFrame fr;
        if (engine.xr) {
            const auto tWait = std::chrono::steady_clock::now();
            engine.xr->waitFrame();
            wait = msBetween(tWait, std::chrono::steady_clock::now());
            fr = engine.xr->poll();
        }
        if (engine.hasWindow) {
            app.pollEvents();
            VkExtent2D base = app.extent();
            if (engine.xrSession && fr.running) { // in VR the display is the headset, not the window
                const VkExtent2D e0 = engine.xr->extent(0);
                const VkExtent2D e1 = engine.xr->extent(engine.xr->eyeCount() > 1 ? 1 : 0);
                base = {std::max(e0.width, e1.width), std::max(e0.height, e1.height)};
            }
            const float scale = readSetting<RenderSettings>(engine.reg).renderScale;
            auto q = [](float v) { return (uint32_t)std::max(16.0f, std::lround(v / 32.0f) * 32.0f); };
            app.setRenderExtent({q((float)base.width * scale), q((float)base.height * scale)});
            app.resizeBuffers();
        }
        if (engine.hasDevice)
            engine.graphics.flushFrees();
        if (!engine.pendingScene.empty()) {
            std::string p;
            std::swap(p, engine.pendingScene);
            if (!loadScene(engine, p) && engine.scenePath != kMenuPath)
                loadScene(engine, kMenuPath);
        }
        const auto tPoll = std::chrono::steady_clock::now();
        if (engine.hasWindow) {
            stepMouse(engine);
            stepKeyboard(engine);
        }
        engine.script.activateScripts();
        engine.script.animateAll(frameDt); // render rate (per frame), as before
        if (engine.xr) {
            stepXr(engine.reg, fr);
            stepControllers(engine.reg, fr);
        }

        float frameJolt = 0.0f, frameSkel = 0.0f, frameSoft = 0.0f, frameAcq = 0.0f;
        if (engine.hasDevice)
            engine.graphics.beginFrame(); // async: pick this frame's ring slot, wait on its fence (no-op in steady state)
        // Fixed-tick loop: every tick runs the whole sim at exactly tickDt. The _physics script hook
        // dispatches inside physics.update (before Jolt), so kinematic poses take effect same-tick.
        applyPhysHz(cliPhysHz ? physHz : readSetting<PhysicsSettings>(engine.reg).physHz);
        int steps = 0;
        while (acc >= tickNs && steps < maxSteps) {
            engine.physics.update(engine.reg, tickDt);
            stepAnimations(engine.reg, tickDt);
            snapshotBones(engine.reg); // capture this tick's bone locals for render interpolation
            resolveWorldTransforms(engine.reg);
            acc -= tickNs;
            ++steps;
        }
        if (steps == maxSteps)
            acc = 0; // hit the cap: drop excess time — the sim slows down instead of spiraling
        // Cloth runs once per rendered frame at frame rate, not per physics tick: record() substeps
        // internally (SoftBodyData.steps over the passed dt), so this keeps its substep size and
        // per-frame work at what they were when physics ran at display rate. After the loop so
        // pinning/capsule sync sees this frame's final bone state.
        if (engine.hasDevice)
            engine.graphics.stepSoftSim(engine.reg, frameDt);
        tickAccum += steps;
        const float alpha = noInterp ? 1.0f : (float)acc / (float)tickNs;
        engine.physics.interpolatePoses(engine.reg, alpha); // render-rate pose between the last two ticks
        interpolateBones(engine.reg, alpha);                // same for skeleton joints (skinning reads them)
        if (engine.hasDevice)
            engine.graphics.setInterpAlpha(alpha); // soft-body vertex mix uses the same alpha
        resolveWorldTransforms(engine.reg); // per frame: covers zero-tick frames & input-driven motion
        if (engine.hasDevice) {
            engine.graphics.stepSkinning(engine.reg);
            frameSkel = engine.graphics.simProf.skel;
            frameSoft = engine.graphics.simProf.soft;
            engine.graphics.simProf.reset();
        }
        frameJolt = engine.physics.takeJoltMs();
        const auto tSim = std::chrono::steady_clock::now();

        entt::entity cam = entt::null;
        if (engine.hasWindow) {
            const auto foundCam = findActiveCamera(engine.reg);
            if (foundCam != entt::null) {
                cam = foundCam;
                engine.graphics.setViewProj(
                    cameraView(engine.reg, cam, (float)app.extent().width / (float)app.extent().height));
            }
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
                std::vector<entt::entity> excl;
                engine.reg.view<Name>().each([&](entt::entity e, const Name& n) {
                    if (std::strstr(n.id.c_str(), "hand") || std::strstr(n.id.c_str(), "controller"))
                        excl.push_back(e);
                });
                const RayHit h = engine.physics.raycast(o, d, 30.0f, excl);
                if (!h.hit || engine.physics.entityFor(h.body) != canvas)
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
                engine.graphics.uiEnsureTarget(engine.reg, c);
                UiFeed f{c, ImVec2{-1.0f, -1.0f}, false, 0.0f, false};
                if (engine.xr && fr.running && fr.havePose) {
                    const entt::entity root = findMarker<VrRoot>(engine.reg);
                    const V3 rootPos = root != entt::null ? worldTransform(engine.reg, root).pos : V3{0, 0, 0};
                    for (int i = 0; i < 2; ++i) {
                        const XrController& ctl = fr.controller[i];
                        if (!ctl.active)
                            continue;
                        const V3 o = rootPos + ctl.aimPos;
                        const V3 d = ctl.aimQuat * V3{0, 0, -1};
                        f.down = ctl.buttons[0];
                        f.wheel = -ctl.axis[1];
                        if (tryRay(c, o, d, f))
                            break;
                    }
                } else if (cam != entt::null) {
                    const CameraView cv =
                        cameraView(engine.reg, cam, (float)app.extent().width / (float)app.extent().height);
                    double mx, my;
                    glfwGetCursorPos(app.glfwWindow(), &mx, &my);
                    const float x = 2.0f * (float)mx / (float)app.extent().width - 1.0f;
                    const float y = 1.0f - 2.0f * (float)my / (float)app.extent().height;
                    const V3 cd = {(x * 0.5f + 0.5f) * (cv.tanR - cv.tanL) + cv.tanL,
                                   (y * 0.5f + 0.5f) * (cv.tanU - cv.tanD) + cv.tanD, -1.0f};
                    const V3 o = cv.pos;
                    const Mat4 c2w = glm::transpose(cv.view);
                    const V3 right = c2w[0];
                    const V3 up = c2w[1];
                    const V3 back = c2w[2];
                    const V3 d = glm::normalize(right * cd.x + up * cd.y + back * cd.z);
                    f.down = glfwGetMouseButton(app.glfwWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                    tryRay(c, o, d, f);
                }
                feeds.push_back(f);
            }
            if (!feeds.empty() && engine.showDemo)
                feeds[0].demo = true;
            for (entt::entity c : canvases)
                uiEnsureContext(engine.reg, c, engine.graphics.uiTargetPass(c), engine.graphics.uiTargetFb(c));
            uiFrame(engine.reg, feeds);
        }
        stepBinds(engine.reg);

        uiAccum += msBetween(tSim, std::chrono::steady_clock::now());
        const auto tAcq = std::chrono::steady_clock::now();
        if (engine.hasWindow) {
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
                engine.graphics.drawVr(cmd, app, idx, engine.reg, cam, fr.shouldRender ? imgIdx : nullptr,
                                       fr.shouldRender ? engine.xr->eyeCount() : 0);
                if (fr.shouldRender)
                    for (int i = 0; i < engine.xr->eyeCount(); ++i)
                        engine.xr->releaseImage(i);
            } else {
                const auto tDraw = std::chrono::steady_clock::now();
                engine.graphics.draw(cmd, app, idx, engine.reg);
                drawAccum += msBetween(tDraw, std::chrono::steady_clock::now());
            }
            app.endFrame(); // async: submit this frame's batched GPU stages with the slot fence (before present)
            if (engine.xr)
                engine.xr->endFrame(fr.shouldRender);
            // Vsync-aligned pacing: the pacing sleep below lands present on the refresh edge. (In --sync mode
            // a FIFO present also blocks until the display refresh; async presents return immediately and the
            // sleep does all the pacing.) An unpaced loop drifts out of phase and occasionally misses an edge
            // by a hair -- then present waits a whole extra frame and the sim takes one big step (a visible
            // jump). Sleeping until just before the next refresh edge makes present land on it every frame.
            if (engine.hasWindow && !engine.xr && app.vsynced()) {
                const auto now = std::chrono::steady_clock::now();
                if (paceEdge <= now)
                    while (paceEdge <= now)
                        paceEdge += pacePeriod; // resync after a long stall
                const auto wake = paceEdge - std::chrono::milliseconds(2);
                if (wake > now)
                    std::this_thread::sleep_until(wake);
            }
            // --sync: drain the queue at present (legacy behavior). Async: present returns once scheduled;
            // the slot fences bound how far ahead the CPU may run.
            app.present(idx, syncSubmit && !engine.xr);
            if (engine.hasWindow && !engine.xr && app.vsynced()) {
                const auto now = std::chrono::steady_clock::now();
                do
                    paceEdge += pacePeriod;
                while (paceEdge <= now);
            }
        } else if (engine.hasDevice) {
            app.endFrame(); // headless: no draw/present — flush the soft/skin batch recorded this frame
        }
        const auto tRender = std::chrono::steady_clock::now();
        const auto tXrEnd = tRender;
        phaseMs.poll += msBetween(nowFrame, tPoll);
        phaseMs.wait += wait;
        phaseMs.cpu += msBetween(tPoll, tSim) - frameJolt - frameSkel - frameSoft;
        phaseMs.jolt += frameJolt;
        phaseMs.skel += frameSkel;
        phaseMs.sim += frameSoft;
        phaseMs.rend += msBetween(tSim, tRender) - frameAcq;
        phaseMs.xend += msBetween(tRender, tXrEnd);
        phaseMs.tail += msBetween(tXrEnd, std::chrono::steady_clock::now()) + frameAcq;
        if (engine.xr)
            pace.add(msBetween(nowFrame, tXrEnd) - wait);
    }

    if (engine.hasWindow)
        uiShutdown();
    if (engine.hasDevice)
        engine.graphics.shutdown();
    engine.script.shutdown();
    if (engine.xr)
        engine.xr->shutdown();
    if (!noGraphics)
        app.shutdown();
    return rc;
}
