#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include "app/geometry.hpp"
#include "app/rigid.hpp"
#include "app/ui.hpp"
#include "capsule.hpp"
#include "math.hpp"
#include "sim/softsim.hpp"
#include "vk/renderer.hpp"
#include "vk/vkapp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace {

constexpr int kCW = 64, kCH = 64;
constexpr int kHoldFrames = 150;
constexpr float kClothSpan = 8.0f;
constexpr float kClothY0 = 10.0f;
constexpr int kBallSubdiv = 2;
constexpr float kBallRadius = 1.5f;
constexpr float kBallY0 = 12.0f;
constexpr int kNCapsules = 50;

struct Soft {
    SoftMesh mesh;
    bool pinned = false;
};

V4 quatAxisAngle(V3 axis, float angle) {
    V3 a = vNorm(axis);
    const float s = std::sin(0.5f * angle);
    return {a.x * s, a.y * s, a.z * s, std::cos(0.5f * angle)};
}

V4 quatMul(V4 a, V4 b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

void scatterCapsules(RigidScene& rigid, std::vector<CapsuleCollider>& colliders, int n) {
    std::mt19937 rng(12345); // NOLINT(bugprone-random-generator-seed)
    std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
    colliders.reserve(n);
    for (int i = 0; i < n; ++i) {
        const float px = (rnd(rng) * 2.0f - 1.0f) * 2.0f;
        const float py = 4.5f + 3.5f * rnd(rng);
        const float pz = (rnd(rng) * 2.0f - 1.0f) * 2.0f;
        const V4 rot =
            quatMul(quatAxisAngle({0, 1, 0}, rnd(rng) * 2.0f * kPi),
                    quatAxisAngle({1, 0, 0}, (rnd(rng) * 2.0f - 1.0f) * 0.9f));
        const CapsuleCollider c(kCapsule, {px, py, pz}, rot);
        rigid.addCapsule(c);
        colliders.push_back(c);
    }
}

int pinnedMask(const std::vector<Soft>& softs, bool held) {
    if (!held)
        return 0;
    int mask = 0;
    for (int i = 0; i < (int)softs.size(); ++i)
        if (softs[i].pinned)
            mask |= 1 << i;
    return mask;
}

} // namespace

int main() {
    VkApp app;
    if (!app.init(900, 900, "3dsim"))
        return 1;

    RigidScene rigid;
    rigid.init();
    std::vector<CapsuleCollider> colliders;
    scatterCapsules(rigid, colliders, kNCapsules);

    std::vector<Soft> softs;
    const int clothIdx = (int)softs.size();
    softs.push_back({makeCloth(kCW, kCH, kClothSpan, kClothY0), true});
    const int ballIdx = (int)softs.size();
    softs.push_back({makeBall(kBallRadius, kBallY0, kBallSubdiv), false});
    const int allBodies = (1 << (int)softs.size()) - 1;

    int frames = 0;
    bool held = true;

    const float aspect = (float)app.extent().width / (float)app.extent().height;
    const Mat4 vp = mul4(perspective(50.0f, aspect, 0.1f, 300.0f),
                         lookAt({0.0f, 9.0f, 14.0f}, {0.0f, 3.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));

    Renderer renderer;
    renderer.init(app, 1 + (int)softs.size(), vp);

    const Mesh capMesh = makeCapsuleMesh(kNCapsules);
    const MeshGpu capRw = renderer.createReadWriteBuffer(capMesh);
    renderer.setCapsuleIndex(renderer.addMesh(capMesh, capRw));

    SoftSim sim;
    sim.init(app.device(), app.pdev());
    for (const CapsuleCollider& c : colliders)
        sim.addCapsule(c);
    for (const Soft& s : softs) {
        const MeshGpu rw = renderer.createReadWriteBuffer(s.mesh.mesh);
        sim.addSoftBody(s.mesh.mesh, s.mesh.cons, rw);
        renderer.addMesh(s.mesh.mesh, rw);
    }
    sim.build();

    auto resetAll = [&] {
        frames = 0;
        held = true;
        sim.reset(allBodies);
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

    UIState ui;
    std::printf("3dsim: vulkan+imgui | GPU soft-body sim | R to reset | esc/close to quit\n");
    const double kStepSec = kFrameDt;
    double prevTime = glfwGetTime();
    double accumulator = 0.0;

    while (!app.windowShouldClose()) {
        if (app.keyIsDown(GLFW_KEY_R))
            resetAll();

        double now = glfwGetTime();
        double frameTime = std::min(now - prevTime, 4.0 * kStepSec);
        prevTime = now;
        accumulator += frameTime;
        int steps = 0;
        while (accumulator >= kStepSec && steps < 4) {
            accumulator -= kStepSec;
            ++steps;
        }
        if (steps > 0) {
            rigid.step(steps);
            frames += steps;
            if (frames >= kHoldFrames)
                held = false;
            sim.syncColliders(rigid.capsulePose());
            VkCommandBuffer simCmd = app.beginCommands();
            for (int s = 0; s < steps; ++s)
                sim.record(simCmd, ui.sim, pinnedMask(softs, held));
            app.submit(simCmd);
        }

        app.pollEvents();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        bool clothReset = false, ballReset = false;
        drawOverlay(ui, held && softs[clothIdx].pinned, clothReset, ballReset);
        if (clothReset)
            resetAll();
        if (ballReset)
            sim.reset(1 << ballIdx);
        ImGui::Render();

        uint32_t idx = app.acquireNextImage();
        VkCommandBuffer cmd = app.beginCommands();
        renderer.draw(cmd, app, idx, ui.bgColor, ImGui::GetDrawData(), rigid.capsuleGPU());
        app.present(idx);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    sim.shutdown();
    renderer.shutdown();
    app.shutdown();
    return 0;
}
