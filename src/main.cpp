#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

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
        // UI pointer: world-space ray (VR: controller aim; window: camera through cursor)
        // -> rigid-body raycast against the panel collider -> uv of the hit point
        const entt::entity panel = findUiPanel(engine.reg);
        V3 headPos{};
        V4 headQuat{0, 0, 0, 1};
        V2 pUv{};
        V3 pHitPos{};
        bool pDown = false;
        float pWheel = 0.0f;
        bool pHit = false;
        auto tryRay = [&](const V3& o, const V3& d) {
            // the hand's own bodies (anchor, controller) hug the ray origin and would win at t=0;
            // exclude them by name so the panel stays castable even when the hand is close
            std::vector<entt::entity> excl;
            engine.reg.view<Name>().each([&](entt::entity e, const Name& n) {
                if (std::strstr(n.id.c_str(), "hand") || std::strstr(n.id.c_str(), "controller"))
                    excl.push_back(e);
            });
            const RayHit h = engine.rigid.raycast(o, d, 30.0f, excl);
            if (!h.hit || engine.rigid.entityFor(h.body) != panel)
                return false;
            pHitPos = h.pos;
            return uiPanelUv(engine.reg, panel, h.pos, pUv);
        };
        if (engine.xr && fr.running && fr.havePose) {
            headPos = fr.eye[0].pos;
            headQuat = fr.eye[0].quat;
            const entt::entity root = findMarker<VrRoot>(engine.reg);
            const V3 rootPos = root != entt::null ? worldTransform(engine.reg, root).pos : V3{0, 0, 0};
            for (int i = 0; i < 2; ++i) {
                const XrController& ctl = fr.controller[i];
                if (!ctl.active)
                    continue;
                const V3 o = vAdd(rootPos, ctl.aimPos); // the aim pose is in the vr-root (locomotion) frame
                const V3 d = quatRotate(ctl.aimQuat, V3{0, 0, -1});
                if (tryRay(o, d)) {
                    pDown = ctl.triggers[0] > 0.3f;
                    pWheel = -ctl.axis[1];
                    pHit = true;
                    break;
                }
                if (!pHit) {
                    pDown = ctl.triggers[0] > 0.3f; // fallback owner: hover off, button state live
                }
            }
            // debounced stdout trace: on hit change, on uv move > 2cm, or once per second
            static int lastSec = -1;
            static float lastUvX = -1.0f, lastUvY = -1.0f;
            static int lastHit = -1;
            for (int i = 0; i < 2; ++i) {
                const XrController& ctl = fr.controller[i];
                if (!ctl.active)
                    continue;
                const V3 o = vAdd(rootPos, ctl.aimPos);
                const V3 d = quatRotate(ctl.aimQuat, V3{0, 0, -1});
                const int sec = (int)std::time(nullptr);
                const bool moved = std::fabs(pUv.x - lastUvX) > 0.02f || std::fabs(pUv.y - lastUvY) > 0.02f;
                if (pHit != lastHit || moved || sec != lastSec) {
                    const V3 pp = panel != entt::null ? worldTransform(engine.reg, panel).pos : V3{0, 0, 0};
                    static bool printedBox = false;
                    if (!printedBox && panel != entt::null) {
                        printedBox = true;
                        const Transform pt = worldTransform(engine.reg, panel);
                        const V3 xh = quatRotate(pt.quat, V3{1, 0, 0});
                        const V3 yh = quatRotate(pt.quat, V3{0, 1, 0});
                        const RayHit hx = engine.rigid.raycast(vSub(pt.pos, vScale(xh, 2.0f)), xh, 2.5f);
                        const RayHit hy = engine.rigid.raycast(vSub(pt.pos, vScale(yh, 2.0f)), yh, 2.5f);
                        const float ex = hx.hit ? 2.0f - (float)vLen(vSub(hx.pos, vSub(pt.pos, vScale(xh, 2.0f)))) : -1.0f;
                        const float ey = hy.hit ? 2.0f - (float)vLen(vSub(hy.pos, vSub(pt.pos, vScale(yh, 2.0f)))) : -1.0f;
                        std::printf("ui body half-extents probed=(%.3f,%.3f) expect=(%.3f,%.3f)\n",
                                   (double)ex, (double)ey, (double)(kPanelW * 0.5f), (double)(kPanelH * 0.5f));
                        const V4 q = pt.quat;
                        const float ql = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
                        const V3 xh2 = quatRotate(q, V3{1, 0, 0});
                        const V3 yh2 = quatRotate(q, V3{0, 1, 0});
                        std::printf("ui panel quat=(%.4f,%.4f,%.4f,%.4f) |q|=%.4f |xh|=%.4f |yh|=%.4f\n",
                                   (double)q.x, (double)q.y, (double)q.z, (double)q.w, (double)ql,
                                   (double)vLen(xh2), (double)vLen(yh2));
                        std::fflush(stdout);
                    }
                    std::printf("ui hit=%d trig=(%.2f/%.2f,%.2f/%.2f) uv=(%.2f,%.2f) o=(%.2f,%.2f,%.2f) d=(%.2f,%.2f,%.2f) panel=(%.2f,%.2f,%.2f) hitpos=(%.2f,%.2f,%.2f)\n",
                               (int)pHit, (double)fr.controller[0].triggers[0], (double)fr.controller[0].triggers[1],
                               (double)fr.controller[1].triggers[0], (double)fr.controller[1].triggers[1],
                               (double)pUv.x, (double)pUv.y, (double)o.x, (double)o.y, (double)o.z,
                               (double)d.x, (double)d.y, (double)d.z, (double)pp.x, (double)pp.y, (double)pp.z,
                               (double)pHitPos.x, (double)pHitPos.y, (double)pHitPos.z);
                    std::fflush(stdout);
                    lastUvX = pUv.x;
                    lastUvY = pUv.y;
                    lastHit = pHit;
                    lastSec = sec;
                }
                break;
            }
        } else if (cam != entt::null) {
            const CameraView cv = cameraView(engine.reg, cam, (float)app.extent().width / (float)app.extent().height);
            headPos = cv.pos;
            headQuat = worldTransform(engine.reg, cam).quat;
            double mx, my;
            glfwGetCursorPos(app.glfwWindow(), &mx, &my);
            // cursor is in window pixels but the camera projects in framebuffer pixels
            const float x = 2.0f * (float)mx / (float)app.extent().width - 1.0f;
            const float y = 1.0f - 2.0f * (float)my / (float)app.extent().height;
            const V3 cd = {(x * 0.5f + 0.5f) * (cv.tanR - cv.tanL) + cv.tanL,
                          (y * 0.5f + 0.5f) * (cv.tanU - cv.tanD) + cv.tanD, -1.0f};
            const float* m = cv.view.m;
            const V3 o = cv.pos;
            const V3 d = vNormalize({cd.x * m[0] + cd.y * m[4] + cd.z * m[8],
                                   cd.x * m[1] + cd.y * m[5] + cd.z * m[9],
                                   cd.x * m[2] + cd.y * m[6] + cd.z * m[10]});
            if (tryRay(o, d)) {
                pDown = glfwGetMouseButton(app.glfwWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                pHit = true;
            }
        }
        if (engine.xr) {
            // the panel is static (placed once): only commit the placement from a focused session,
            // so a headset-off (visible) start doesn't place it facing the stale default head pose
            if (fr.focused && fr.havePose)
                updateUiPanel(engine.reg, panel, headPos, headQuat);
        } else if (cam != entt::null) {
            updateUiPanel(engine.reg, panel, headPos, headQuat);
        }
        if (pHit || (engine.xr && fr.running && (fr.controller[0].active || fr.controller[1].active)) ||
            (engine.xr == nullptr && cam != entt::null)) {
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, pHit || !engine.xr ? pDown : false);
            if (pWheel != 0.0f)
                ImGui::GetIO().AddMouseWheelEvent(0.0f, pWheel);
        } else {
            // keep the physical (OS) mouse from hijacking the pointer: we own io.MousePos below
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        }
        imguiNewFrame(
            (float)engine.renderer.uiWidth(), 
            (float)engine.renderer.uiHeight(),
            pHit ? ImVec2(pUv.x * (float)engine.renderer.uiWidth(),
                                               (1.0f - pUv.y) * (float)engine.renderer.uiHeight())
                                      : ImVec2(-1.0f, -1.0f)
        );
        // direct write after NewFrame so it wins over any GLFW backend mouse events this frame
        //ImGui::GetIO().MousePos = ;
        drawUi(engine);
        if (pHit) {
            const ImVec2 dp(pUv.x * (float)engine.renderer.uiWidth(), (1.0f - pUv.y) * (float)engine.renderer.uiHeight());
            auto* dl = ImGui::GetForegroundDrawList();
            dl->AddCircle(dp, 14.0f, IM_COL32(255, 0, 0, 255), 24);
            dl->AddCircle(dp, 3.0f, IM_COL32(255, 0, 0, 255));
        }
        if (engine.showDemo)
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
            Transform panelT;
            const Transform* panelP = nullptr;
            if (panel != entt::null) {
                panelT = worldTransform(engine.reg, panel);
                panelP = &panelT;
            }
            engine.renderer.drawVr(cmd, app, idx, engine.reg, engine.render, ImGui::GetDrawData(), cam,
                              fr.shouldRender ? imgIdx : nullptr, fr.shouldRender ? engine.xr->eyeCount() : 0,
                              panelP);
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
