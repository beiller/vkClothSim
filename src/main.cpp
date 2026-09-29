#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "demo.hpp"
#include "hdri.hpp"
#include "shadowtest.hpp"
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
    bool wantHdri = false, wantShadow = false, dumpFrames = false, wantVr = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hdri") == 0)
            wantHdri = true;
        else if (std::strcmp(argv[i], "--shadow") == 0)
            wantShadow = true;
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
    if (wantHdri)
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
            std::printf("3dsim: no OpenXR session, using fallback VR pose\n");
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
    int frame = 0;
    while (!app.windowShouldClose()) {
        const auto nowFrame = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(nowFrame - lastFrame).count();
        lastFrame = nowFrame;
        if (app.keyIsDown(GLFW_KEY_R))
            resetSofts(w);
        stepPinHolds(w, dt);

        w.rigid.setVelocitySteps(w.ui.joltIters);
        stepRigid(w);
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
                // no headset: view from the scene's default camera with a small stereo
                // offset, so the debug view matches the scene's intended composition
                const float tl = std::tan(95.0f * 0.5f * kPi / 180.0f);
                const float tu = std::tan(60.0f * 0.5f * kPi / 180.0f);
                const V3 head = vAdd(w.camera.position, rig);
                const V4 q = w.camera.rotation;
                const float qf[4] = {q.x, q.y, q.z, q.w};
                float rotm[9];
                quatToMat3(qf, rotm);
                const V3 right{rotm[0], rotm[1], rotm[2]};
                for (int i = 0; i < 2; ++i) {
                    eyes[i].pos = vAdd(head, vScale(right, i == 0 ? -0.032f : 0.032f));
                    eyes[i].quat = q;
                    eyes[i].tanL = -tl;
                    eyes[i].tanR = tl;
                    eyes[i].tanU = tu;
                    eyes[i].tanD = -tu;
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
