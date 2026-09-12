// main.cpp
// The app: the main loop that ties the pieces together. VkApp owns the window + the
// Vulkan core; Scene owns the simulation (the rigid capsules + the soft cloth/ball);
// Renderer owns the GPU (pipelines + buffers + the per-frame draw); ui.{hpp,cpp} the
// ImGui overlay.
//   --shot out.ppm [--steps N] : render one frame (the settled drape), write a PPM, exit
//                               (headless verification; N = physics frames to settle,
//                               default 150).
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "vk/vkapp.hpp"
#include "vk/renderer.hpp"
#include "app/scene.hpp"
#include "app/ui.hpp"
#include "math.hpp"

int main(int argc, char** argv) {
    bool shot = false;
    std::string shotPath = "shot.ppm";
    int shotSteps = 450;   // enough for the damped cloth to land + drape (damping 0.96 falls slowly)
    int shotStiff = -1;   // override the relax iterations (for tuning the GPU drape)
    float shotRscale = 1.0f;   // the relax scale (softer constraints -> more drape)
    float shotDamp = -1.0f;    // override the damping (for stability tuning)
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--shot") {
            shot = true;
            if (i + 1 < argc)
                shotPath = argv[++i];
        } else if (std::string(argv[i]) == "--steps") {
            if (i + 1 < argc)
                shotSteps = std::atoi(argv[++i]);
        } else if (std::string(argv[i]) == "--stiff") {
            if (i + 1 < argc)
                shotStiff = std::atoi(argv[++i]);
        } else if (std::string(argv[i]) == "--rscale") {
            if (i + 1 < argc)
                shotRscale = std::atof(argv[++i]);
        } else if (std::string(argv[i]) == "--damp") {
            if (i + 1 < argc)
                shotDamp = std::atof(argv[++i]);
        }

    // the window + the Vulkan core
    VkApp app;
    if (!app.init(900, 900, "3dsim"))
        return 1;

    // the simulation (the rigid capsules + the soft cloth/ball)
    Scene scene;
    scene.init();

    // fixed camera looking at the capsule field
    const float aspect = (float)app.extent().width / (float)app.extent().height;
    const Mat4 vp = mul4(perspective(50.0f, aspect, 0.1f, 300.0f),
                         lookAt({0.0f, 9.0f, 14.0f}, {0.0f, 3.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));

    // the GPU (the pipelines + the per-frame uploads/draws)
    Renderer renderer;
    renderer.init(app, scene, vp);

    // the ImGui context + the GLFW/Vulkan backends (drawn into the same swapchain)
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
    ii.CheckVkResultFn = [](VkResult err) { if (err != VK_SUCCESS) std::printf("ImGui VK err %d\n", (int)err); };
    ImGui_ImplVulkan_Init(&ii);

    UIState ui;

    if (shot) {
        // settle the scene: the SAME per-frame unified step as the live loop, except the soft
        // bodies run on the GPU (one recordSoftSim per step; no render until the end).
        if (shotStiff >= 0)
            ui.sim.stiffness = shotStiff;
        if (shotDamp >= 0.0f)
            ui.sim.damping = shotDamp;
        scene.unpin();                        // drop the cloth for the whole shot
        for (int i = 0; i < shotSteps; ++i) {
            scene.stepRigid(1);
            renderer.uploadCapsules(scene.rigid().capsuleGPU());
            renderer.stepSoftFrame(app, ui.sim, scene.clothPinned() ? 1 : 0, shotRscale);
            if ((i + 1) % 150 == 0) {   // trace the cloth y-range to catch a developing oscillation
                std::vector<float> cp;
                renderer.readbackBody(app, "cloth", cp);
                float lo = 1e9f, hi = -1e9f;
                for (int j = 0; j < (int)cp.size() / 4; ++j) {
                    lo = std::min(lo, cp[4 * j + 1]); hi = std::max(hi, cp[4 * j + 1]);
                }
                std::printf("  step %4d: cloth y=[%.2f, %.2f]\n", i, lo, hi);
            }
        }
        // read back the settled state (the cloth's drape + the ball's landing) for diagnostics
        std::vector<float> clothPos, ballPos;
        renderer.readbackBody(app, "cloth", clothPos);
        renderer.readbackBody(app, "ball", ballPos);
        app.pollEvents();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        bool clothReset = false, ballReset = false;
        drawOverlay(ui, scene.clothPinned(), clothReset, ballReset);
        ImGui::Render();
        renderer.uploadCapsules(scene.rigid().capsuleGPU());
        renderer.draw(app, 0, ui.bgColor, ImGui::GetDrawData(), false, ui.sim, 0);
        app.readbackPPM(0, shotPath);
        // headless diagnostics: the settled state (the cloth's drape + the ball's landing)
        {
            int cn = (int)clothPos.size() / 4;
            float lo = 1e9f, hi = -1e9f, cx0 = 1e9f, cx1 = -1e9f, cz0 = 1e9f, cz1 = -1e9f;
            for (int i = 0; i < cn; ++i) {
                float x = clothPos[4 * i], y = clothPos[4 * i + 1], z = clothPos[4 * i + 2];
                lo = std::min(lo, y); hi = std::max(hi, y);
                cx0 = std::min(cx0, x); cx1 = std::max(cx1, x);
                cz0 = std::min(cz0, z); cz1 = std::max(cz1, z);
            }
            std::printf("  cloth x=[%.2f,%.2f] z=[%.2f,%.2f]\n", cx0, cx1, cz0, cz1);
            // diagnostic: how far are the cloth joints from their rest lengths? (relax working
            // -> tiny; a divergent sim -> large.) Catches the explosion mode headlessly.
            {
                const int CW = Scene::kCW, CH = Scene::kCH;
                const float spacing = Scene::kClothSpan / (CW - 1), diag = spacing * 1.41421356f;
                auto V = [&](int i) { return std::array<float, 3>{clothPos[4 * i], clothPos[4 * i + 1], clothPos[4 * i + 2]}; };
                auto L = [&](const std::array<float, 3>& a, const std::array<float, 3>& b) {
                    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
                    return std::sqrt(dx * dx + dy * dy + dz * dz);
                };
                float mxEdge = 0, mxDiag = 0;
                for (int i = 0; i < cn; ++i) {
                    int gx = i % CW, gy = i / CW;
                    if (gx < CW - 1) mxEdge = std::max(mxEdge, std::abs(L(V(i), V(i + 1)) - spacing));
                    if (gy < CH - 1) mxEdge = std::max(mxEdge, std::abs(L(V(i), V(i + CW)) - spacing));
                    if (gx < CW - 1 && gy < CH - 1) mxDiag = std::max(mxDiag, std::abs(L(V(i), V(i + CW + 1)) - diag));
                    if (gx > 0 && gy < CH - 1) mxDiag = std::max(mxDiag, std::abs(L(V(i), V(i + CW - 1)) - diag));
                }
                std::printf("  joint error: max edge dev=%.4f m  max diag dev=%.4f m (rest %.4f/%.4f)\n",
                            mxEdge, mxDiag, spacing, diag);
            }
            int bn = (int)ballPos.size() / 4;
            float bcx = 0, bcy = 0, bcz = 0;
            for (int i = 0; i < bn; ++i) {
                bcx += ballPos[4 * i]; bcy += ballPos[4 * i + 1]; bcz += ballPos[4 * i + 2];
            }
            bcx /= bn; bcy /= bn; bcz /= bn;
            bool nan = std::isnan(lo) || std::isnan(hi) || std::isnan(bcx) || std::isnan(bcy) || std::isnan(bcz);
            std::printf("3dsim: settled | cloth y=[%.2f,%.2f] ball c=(%.2f,%.2f,%.2f)%s\n",
                        lo, hi, bcx, bcy, bcz, nan ? "  [NaN! -> explosion]" : "");
        }
        // where is the pile + does the cloth actually contact it?
        {
            const auto caps = scene.rigid().capsuleGPU();
            float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f, mnz = 1e9f, mxz = -1e9f;
            for (auto& c : caps) {
                mnx = std::min(mnx, c.centerRadius[0]); mxx = std::max(mxx, c.centerRadius[0]);
                mny = std::min(mny, c.centerRadius[1]); mxy = std::max(mxy, c.centerRadius[1]);
                mnz = std::min(mnz, c.centerRadius[2]); mxz = std::max(mxz, c.centerRadius[2]);
            }
            int nc = (int)caps.size();
            float ccx = 0, ccy = 0, ccz = 0;
            for (auto& c : caps) { ccx += c.centerRadius[0]; ccy += c.centerRadius[1]; ccz += c.centerRadius[2]; }
            ccx /= nc; ccy /= nc; ccz /= nc;
            std::printf("  pile: x=[%.1f,%.1f] y=[%.1f,%.1f] z=[%.1f,%.1f] centroid=(%.2f,%.2f,%.2f) n=%d\n", mnx, mxx, mny, mxy, mnz, mxz, ccx, ccy, ccz, nc);
            const auto col = scene.rigid().colliders();
            int cn = (int)clothPos.size() / 4;
            int contact = 0;
            for (int i = 0; i < cn; ++i) {
                float px = clothPos[4 * i], py = clothPos[4 * i + 1], pz = clothPos[4 * i + 2];
                for (const auto& c : col) {
                    float abx = c.b[0] - c.a[0], aby = c.b[1] - c.a[1], abz = c.b[2] - c.a[2];
                    float t = ((px - c.a[0]) * abx + (py - c.a[1]) * aby + (pz - c.a[2]) * abz) /
                              std::fmax(abx * abx + aby * aby + abz * abz, 1e-6f);
                    t = std::fmax(0.0f, std::fmin(1.0f, t));
                    float dx = px - (c.a[0] + abx * t), dy = py - (c.a[1] + aby * t), dz = pz - (c.a[2] + abz * t);
                    if (std::sqrt(dx * dx + dy * dy + dz * dz) < c.r + 0.02f) { contact++; break; }
                }
            }
            std::printf("  cloth contact with pile: %d / %d verts\n", contact, cn);
            // CPU-side penetration check on the GPU result (max depth inside a capsule)
            float maxPen = 0.0f; int penVerts = 0;
            for (int i = 0; i < cn; ++i) {
                float px = clothPos[4 * i], py = clothPos[4 * i + 1], pz = clothPos[4 * i + 2];
                for (const auto& c : col) {
                    float abx = c.b[0] - c.a[0], aby = c.b[1] - c.a[1], abz = c.b[2] - c.a[2];
                    float t = ((px - c.a[0]) * abx + (py - c.a[1]) * aby + (pz - c.a[2]) * abz) /
                              std::fmax(abx * abx + aby * aby + abz * abz, 1e-6f);
                    t = std::fmax(0.0f, std::fmin(1.0f, t));
                    float dx = px - (c.a[0] + abx * t), dy = py - (c.a[1] + aby * t), dz = pz - (c.a[2] + abz * t);
                    float pen = c.r - std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (pen > maxPen) maxPen = pen;
                    if (pen > 0.0f) penVerts++;
                }
            }
            std::printf("  GPU-cloth penetration: max=%+.4f m in %d verts\n", maxPen, penVerts);
        }
    } else {
        std::printf("3dsim: vulkan+imgui | GPU soft-body sim | R to reset | esc/close to quit\n");
        while (!app.windowShouldClose()) {
            // input + physics (the rigid capsules on the CPU; the soft bodies step on the GPU)
            if (app.keyIsDown(GLFW_KEY_R)) {
                scene.reset();
                renderer.resetSoftBodies();
            }
            scene.stepRigid(1);
            renderer.uploadCapsules(scene.rigid().capsuleGPU());   // -> GPU (the sim + render read them)
            // the ImGui overlay
            app.pollEvents();
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            bool clothReset = false, ballReset = false;
            drawOverlay(ui, scene.clothPinned(), clothReset, ballReset);
            if (clothReset) {
                scene.reset();
                renderer.resetSoftBodies();
            }
            if (ballReset)
                renderer.resetBall();
            ImGui::Render();
            // the GPU soft-body sim (recordSoftSim) + the render + present (one command buffer)
            uint32_t idx = app.acquireNextImage();
            renderer.draw(app, idx, ui.bgColor, ImGui::GetDrawData(), true, ui.sim,
                          scene.clothPinned() ? 1 : 0);
            app.present(idx);
        }
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    renderer.shutdown();
    app.shutdown();
    return 0;
}
