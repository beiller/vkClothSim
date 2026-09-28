#include "shadowtest.hpp"

#include "assets.hpp"
#include "meshgen.hpp"
#include "scene_common.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdint>
#include <imgui.h>

namespace {

void drawShadowUi(World& w) {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, 720), ImGuiCond_Always);
    ImGui::Begin("shadow test");
    for (auto [entity, mat] : w.reg.view<Material>().each()) {
        const auto* name = w.reg.try_get<Name>(entity);
        if (!name)
            continue;
        ImGui::PushID(name->id.c_str());
        ImGui::SeparatorText(name->id.c_str());
        ImGui::ColorEdit3("base color", &mat.baseColor.x);
        ImGui::SliderFloat("metallic", &mat.metallic, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("roughness", &mat.roughness, 0.0f, 1.0f, "%.2f");
        ImGui::PopID();
    }
    ImGui::SeparatorText("camera");
    ImGui::SliderFloat("fov", &w.camera.fovDeg, 10.0f, 120.0f, "%.0f");
    drawToneSection(w);
    drawLightSection(w);
    ImGui::End();
}

} // namespace

void createShadowTestWorld(World& w) {
    w.camera.position = {0.0f, 4.0f, 11.0f};
    w.camera.rotation = quatAxisAngle({1.0f, 0.0f, 0.0f}, -std::atan2f(2.0f, 11.0f));
    w.drawUi = drawShadowUi;
    w.ui.light = {V3{5.0f, 7.0f, 3.0f}, V3{1.0f, 0.97f, 0.92f}, 300.0f, 0.5f, 1.0f};
    w.ui.envIntensity = 0.0f;

    const VkExtent2D ext = w.app->extent();
    w.rigid.init();
    w.sim.init(w.app->device(), w.app->pdev());
    w.renderer.init(*w.app, 2, w.camera.viewProj((float)ext.width / (float)ext.height));

    const entt::entity ground = spawnStaticMesh(w, makeGroundMesh(40.0f), Transform{});
    w.reg.emplace<Name>(ground, "ground");
    w.reg.emplace<Material>(ground, V3{0.6f, 0.58f, 0.52f}, 0.0f, 0.9f);

    const entt::entity ball = spawnStaticMesh(w, makeSphereMesh(4, 2.0f, V3{0.0f, 3.0f, 0.0f}), Transform{});
    w.reg.emplace<Name>(ball, "ball");
    w.reg.emplace<Material>(ball, V3{0.85f, 0.4f, 0.3f}, 0.0f, 0.35f);

    w.sim.build();

    applyEnvHdr(w);
}
