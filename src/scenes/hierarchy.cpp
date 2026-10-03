#include "scenes/hierarchy.hpp"

#include "capsule.hpp"
#include "meshgen.hpp"
#include "scenes/scene_common.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <functional>
#include <imgui.h>
#include <memory>
#include <random>

namespace {

constexpr int kMaxDepth = 5;

// depth -> capsule size: thick trunk tapering to thin twigs
const float kHalfLen[6] = {0.7f, 1.0f, 0.7f, 0.5f, 0.35f, 0.25f};
const float kRadius[6] = {0.5f, 0.4f, 0.3f, 0.22f, 0.15f, 0.1f};
// brown trunk/branches -> green foliage at the tips
const V3 kDepthColor[6] = {
    {0.30f, 0.21f, 0.13f}, // 0 base
    {0.34f, 0.24f, 0.15f}, // 1 trunk
    {0.32f, 0.22f, 0.14f}, // 2 main branch
    {0.20f, 0.38f, 0.18f}, // 3 foliage
    {0.30f, 0.48f, 0.22f}, // 4 foliage
    {0.45f, 0.62f, 0.30f}, // 5 foliage
};

void drawHierarchyUi(World& w) {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340, 400), ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Appearing);
    ImGui::Begin("hierarchy");
    ImGui::TextDisabled("fractal tree, depth 0-%d (trunk -> canopy)", kMaxDepth);
    ImGui::TextDisabled("each generation bends outward; capsules taper and");
    ImGui::TextDisabled("point along their branch; sways in a sin-wave breeze");
    drawToneSection(w);
    drawLightSection(w);
    ImGui::End();
}

} // namespace

void createHierarchyWorld(World& w) {
    const entt::entity cam =
        spawnCamera(w, V3{0.0f, 4.5f, 15.0f}, quatAxisAngle({1.0f, 0.0f, 0.0f}, -std::atan2(1.5f, 15.0f)), 50.0f);
    w.drawUi = drawHierarchyUi;
    spawnLight(w, V3{7.0f, 12.0f, 5.0f}, PointLight{V3{1.0f, 0.97f, 0.92f}, 200.0f, 0.6f, 1.0f}, "sun");
    w.ui.envIntensity = 0.0f;

    const VkExtent2D ext = w.app->extent();
    w.rigid.init();
    w.sim.init(w.app->device(), w.app->pdev());
    // ~100 branches; the exact count is fixed by the branching below
    w.renderer.init(*w.app, 140, cameraViewProj(w, cam, (float)ext.width / (float)ext.height));

    const entt::entity ground = spawnStaticMesh(w, makeGroundMesh(28.0f), Transform{});
    w.reg.emplace<Name>(ground, "ground");
    w.reg.emplace<Material>(ground, V3{0.16f, 0.20f, 0.14f}, 0.0f, 0.9f);

    int geomDepth[kMaxDepth + 1];
    for (int d = 0; d <= kMaxDepth; ++d)
        geomDepth[d] = w.renderer.addMesh(makeCapsuleMesh(CapsuleParams{kHalfLen[d], kRadius[d]}, 1)).geom;

    std::mt19937 rng(20260929);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    auto randUnit = [&]() {
        const V3 v{2.0f * u01(rng) - 1.0f, 2.0f * u01(rng) - 1.0f, 2.0f * u01(rng) - 1.0f};
        const float l = vLen(v);
        return l > 1e-3f ? vScale(v, 1.0f / l) : V3{0.0f, 1.0f, 0.0f};
    };
    // segment length: capsule overhangs its joint so branch pieces connect
    auto edgeLen = [](int depth) {
        return 1.5f * (kHalfLen[depth] + kRadius[depth]);
    };
    // bend this generation away from its parent's direction (main branches bend most)
    auto spreadDir = [&](const V3& pdir, int depth) {
        const float ang = (1.05f - 0.12f * depth) * (0.75f + 0.5f * u01(rng));
        V3 axis = vCross(pdir, randUnit());
        if (vLen(axis) < 0.1f)
            axis = V3{0.0f, 0.0f, 1.0f};
        return vNorm(quatRotate(quatAxisAngle(axis, ang), pdir));
    };
    auto childCount = [](int depth) {
        switch (depth) {
        case 0:
            return 1; // trunk
        case 1:
            return 6; // main branches
        case 2:
            return 3;
        case 3:
            return 2;
        case 4:
            return 1;
        default:
            return 0;
        }
    };

    // one shared breeze clock: the root (depth 0) advances it, every branch reads it
    auto windTime = std::make_shared<float>(0.0f);
    const V3 windAxis{0.0f, 0.0f, 1.0f}; // local-frame bend axis
    const float windFreq = 1.0f;

    int nodeCount = 0;
    // builds the tree in world space, then stores each node's local transform
    std::function<void(entt::entity, V4, V3, V3, int)> addBranch = [&](entt::entity parent, V4 parentWQ, V3 parentWPos,
                                                                       V3 parentDir, int depth) {
        const V3 dir = depth <= 1 ? V3{0.0f, 1.0f, 0.0f} : spreadDir(parentDir, depth);
        const V3 wpos = depth == 0 ? V3{0.0f, 1.0f, 0.0f} : vAdd(parentWPos, vScale(dir, edgeLen(depth)));
        const V4 nodeWQ = quatFromTo({0.0f, 1.0f, 0.0f}, vNorm(dir));
        const V3 localPos = depth == 0 ? wpos : quatRotate(quatInv(parentWQ), vSub(wpos, parentWPos));
        const V4 localQuat = depth == 0 ? nodeWQ : quatMul(quatInv(parentWQ), nodeWQ);

        entt::entity e = w.reg.create();
        w.reg.emplace<Transform>(e, localPos, localQuat);
        if (parent != entt::null)
            w.reg.emplace<Parent>(e, parent);
        w.reg.emplace<Renderable>(e, geomDepth[depth], w.renderer.addInstance(geomDepth[depth]));
        w.reg.emplace<Material>(e, kDepthColor[depth], 0.1f, 0.6f);
        const float amp = 0.02f + 0.02f * depth; // tips sway more
        const float phase = 0.35f * depth;       // outward lag -> bend travels to the tips
        w.reg.emplace<Animation>(e, [localPos, localQuat, amp, phase, windAxis, windFreq, windTime,
                                     isClock = depth == 0](Transform& tr, float dt) {
            if (isClock)
                *windTime += dt;
            const V4 sway = quatAxisAngle(windAxis, amp * std::sin(windFreq * (*windTime) + phase));
            tr.pos = quatRotate(sway, localPos);
            tr.quat = quatMul(sway, localQuat);
        });
        ++nodeCount;
        if (depth >= kMaxDepth)
            return;
        const int n = childCount(depth);
        for (int i = 0; i < n; ++i)
            addBranch(e, nodeWQ, wpos, dir, depth + 1);
    };

    addBranch(entt::null, V4{0, 0, 0, 1}, V3{0, 0, 0}, V3{0, 1, 0}, 0);

    w.sim.build();
    applyEnvHdr(w);
}
