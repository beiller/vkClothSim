#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "scenes/demo.hpp"
#include "scenes/hdri.hpp"
#include "scenes/hierarchy.hpp"
#include "scenes/shadowtest.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include "world.hpp"
#ifdef WITH_OPENXR
#include "xr/xr.hpp"
#endif
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
    if (std::getenv("VR_SCENE_CAM"))
        w.ui.vrSceneCamPose = true;
    if (const char* ro = std::getenv("VR_RIG_OFFSET"))
        std::sscanf(ro, "%f,%f,%f", &w.ui.vrRigOffset[0], &w.ui.vrRigOffset[1], &w.ui.vrRigOffset[2]);
    bool wantHdri = false, wantShadow = false, wantHier = false, dumpFrames = false, wantVr = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hdri") == 0)
            wantHdri = true;
        else if (std::strcmp(argv[i], "--shadow") == 0)
            wantShadow = true;
        else if (std::strcmp(argv[i], "--hier") == 0)
            wantHier = true;
        else if (std::strcmp(argv[i], "--dump") == 0)
            dumpFrames = true;
        else if (std::strcmp(argv[i], "--vr") == 0)
            wantVr = true;
    }
    // create the device before the scene: the sim/renderer allocate on it during setup
#ifdef WITH_OPENXR
    bool vr = wantVr;
    bool haveXrDev = false;
    if (vr && w.xr.createDevice(app))
        haveXrDev = true;
    app.initDevice(haveXrDev ? w.xr.device() : app.makeDevice());
#else
    bool vr = false;
    app.initDevice(app.makeDevice());
#endif
    if (wantHier)
        createHierarchyWorld(w);
    else if (wantHdri)
        createHdriWorld(w);
    else if (wantShadow)
        createShadowTestWorld(w);
    else
        createDemoWorld(w);
#ifdef WITH_OPENXR
    w.vrCam = spawnVrCamera(w, "vr camera");
    if (vr) {
        if (haveXrDev && w.xr.init(app)) {
            std::vector<std::vector<VkImage>> imgs(w.xr.eyeCount());
            VkExtent2D exts[2];
            for (int i = 0; i < 2; ++i) {
                exts[i] = w.xr.extent(i < w.xr.eyeCount() ? i : 0);
                if (i < w.xr.eyeCount())
                    imgs[i] = w.xr.images(i);
            }
            w.renderer.initXrTarget(app, w.xr.format(), w.xr.eyeCount(), imgs, exts);
        } else
            std::printf("3dsim: no OpenXR session, using virtual head (LMB drag = look, WASD/QE = move, shift = fast)\n");
    }
#else
    (void)wantVr;
#endif
    auto makeVP = [&w](VkExtent2D ext) {
        return w.camera.viewProj((float)ext.width / (float)ext.height);
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

    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | R reset | S dump shadow map | --vr side-by-side VR | esc/close to quit\n");

    auto lastFrame = std::chrono::steady_clock::now();
    bool dumpWasDown = false;
    double prevCX = 0.0, prevCY = 0.0;
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
        resolveWorldTransforms(w);
        syncColliders(w);
        VkCommandBuffer simCmd = app.beginCommands();
        stepSoft(w, simCmd);
        app.submit(simCmd);

        app.pollEvents();
#ifdef WITH_OPENXR
        const XrFrameData fr = vr ? w.xr.poll() : XrFrameData{};
        if (vr) {
            syncVrCamera(w, fr);
            V3 rig{0, 0, 0};
            if (w.vrCam != entt::null)
                rig = w.reg.get<VrCamera>(w.vrCam).rigPos;
            rig = vAdd(rig, V3{w.ui.vrRigOffset[0], w.ui.vrRigOffset[1], w.ui.vrRigOffset[2]});
            XrEyeData eyes[2]{};
            int nEyes = 2;
            const bool sceneCamPose = w.ui.vrSceneCamPose;
            if (fr.havePose && fr.nEyes > 0 && !sceneCamPose) {
                nEyes = fr.nEyes;
                for (int i = 0; i < nEyes; ++i) {
                    eyes[i] = fr.eye[i];
                    eyes[i].pos = vAdd(eyes[i].pos, rig);
                }
            } else if (fr.havePose && fr.nEyes > 0) {
                // debug: scene camera pose + headset FOV tangents (isolate projection vs pose)
                nEyes = fr.nEyes;
                const V3 head = vAdd(w.camera.position, rig);
                const V4 q = w.camera.rotation;
                const float qf[4] = {q.x, q.y, q.z, q.w};
                float rotm[9];
                quatToMat3(qf, rotm);
                const V3 right{rotm[0], rotm[1], rotm[2]};
                for (int i = 0; i < nEyes; ++i) {
                    eyes[i] = fr.eye[i]; // keep the headset tangents
                    eyes[i].pos = vAdd(head, vScale(right, i == 0 ? -0.032f : 0.032f));
                    eyes[i].quat = q;
                }
            } else {
                // no headset: virtual head (LMB drag = look, WASD/QE = move) with Quest 3 lens tangents
                VirtHead& vh = w.ui.virtHead;
                if (!vh.init) {
                    vh.pos = w.camera.position;
                    vh.quat = w.camera.rotation;
                    vh.init = true;
                }
                const float qf[4] = {vh.quat.x, vh.quat.y, vh.quat.z, vh.quat.w};
                float rotm[9];
                quatToMat3(qf, rotm);
                const V3 right{rotm[0], rotm[1], rotm[2]};
                const V3 back{rotm[6], rotm[7], rotm[8]};
                ImGuiIO& io = ImGui::GetIO();
                double cx, cy;
                glfwGetCursorPos(app.glfwWindow(), &cx, &cy);
                if (vh.init && !io.WantCaptureMouse &&
                    glfwGetMouseButton(app.glfwWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                    const float sens = 0.0025f;
                    const float dx = (float)(cx - prevCX);
                    const float dy = (float)(cy - prevCY);
                    // yaw: world-Y premultiply; pitch: local-X postmultiply (keeps the horizon level)
                    vh.quat = quatMul(quatAxisAngle({0, 1, 0}, -dx * sens),
                                      quatMul(vh.quat, quatAxisAngle({1, 0, 0}, -dy * sens)));
                }
                prevCX = cx;
                prevCY = cy;
                V3 mv{0, 0, 0};
                if (!io.WantCaptureKeyboard) {
                    if (app.keyIsDown(GLFW_KEY_W)) mv = vAdd(mv, vScale(back, -1.0f));
                    if (app.keyIsDown(GLFW_KEY_S)) mv = vAdd(mv, back);
                    if (app.keyIsDown(GLFW_KEY_D)) mv = vAdd(mv, right);
                    if (app.keyIsDown(GLFW_KEY_A)) mv = vAdd(mv, vScale(right, -1.0f));
                    if (app.keyIsDown(GLFW_KEY_E)) mv = vAdd(mv, V3{0, 1, 0});
                    if (app.keyIsDown(GLFW_KEY_Q)) mv = vAdd(mv, V3{0, -1, 0});
                }
                if (vLen(mv) > 0.0f)
                    vh.pos = vAdd(vh.pos, vScale(vNorm(mv), (app.keyIsDown(GLFW_KEY_LEFT_SHIFT) ? 6.0f : 2.0f) * dt));
                const V3 head = vAdd(vh.pos, rig);
                for (int i = 0; i < 2; ++i) {
                    eyes[i].pos = vAdd(head, vScale(right, i == 0 ? -0.032f : 0.032f));
                    eyes[i].quat = vh.quat;
                    eyes[i].tanL = -std::tan(54.0f * kPi / 180.0f);
                    eyes[i].tanR = std::tan(40.0f * kPi / 180.0f);
                    eyes[i].tanU = std::tan(44.0f * kPi / 180.0f);
                    eyes[i].tanD = -std::tan(55.0f * kPi / 180.0f);
                }
            }
            if (std::getenv("VR_FULLVIEW"))
                nEyes = 1;
            w.renderer.setVrEyes(eyes, nEyes);
        }
#endif
        w.renderer.setViewProj(makeVP(app.extent()), w.camera.position);
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
#ifdef WITH_OPENXR
        if (vr) {
            syncSceneToRenderer(w);
            uint32_t imgIdx[2] = {0, 0};
            if (fr.shouldRender)
                for (int i = 0; i < w.xr.eyeCount(); ++i) {
                    w.xr.acquireImage(i);
                    imgIdx[i] = (uint32_t)w.xr.imageIndex(i);
                }
            w.renderer.drawVr(cmd, app, idx, w.ui.bgColor, ImGui::GetDrawData(), w.ui.exposure,
                              fr.shouldRender ? imgIdx : nullptr, fr.shouldRender ? w.xr.eyeCount() : 0);
            if (fr.shouldRender)
                for (int i = 0; i < w.xr.eyeCount(); ++i)
                    w.xr.releaseImage(i);
            w.xr.endFrame(fr.shouldRender);
        } else
#endif
        {
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
#ifdef WITH_OPENXR
    w.xr.shutdown();
#endif
    app.shutdown();
    return 0;
}
