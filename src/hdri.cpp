#include "hdri.hpp"

#include "assets.hpp"
#include "meshgen.hpp"
#include "scene_common.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdint>
#include <imgui.h>

namespace {

void drawHdriUi(World& w) {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, 680), ImGuiCond_Always);
    ImGui::Begin("hdri");
    for (auto [entity, mat] : w.reg.view<Material>().each()) {
        const auto* name = w.reg.try_get<Name>(entity);
        if (!name || name->id != "sphere")
            continue;
        ImGui::ColorEdit3("base color", &mat.baseColor.x);
        ImGui::SliderFloat("metallic", &mat.metallic, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("roughness", &mat.roughness, 0.0f, 1.0f, "%.2f");
    }
    ImGui::SeparatorText("camera");
    ImGui::SliderFloat("fov", &w.camera.fovDeg, 10.0f, 120.0f, "%.0f");
    drawToneSection(w);
    drawLightSection(w);
    ImGui::End();
}

} // namespace

void createHdriWorld(World& w) {
    w.camera.position = {0.0f, 0.0f, 6.0f};
    w.camera.rotation = V4{0.0f, 0.0f, 0.0f, 1.0f};
    w.drawUi = drawHdriUi;
    spawnLight(w, V3{2.0f, 2.0f, 3.0f}, PointLight{V3{1.0f, 1.0f, 1.0f}, 20.0f, 0.15f, 1.0f}, "light");

    const VkExtent2D ext = w.app->extent();
    w.rigid.init();
    w.sim.init(w.app->device(), w.app->pdev());
    w.renderer.init(*w.app, 1, w.camera.viewProj((float)ext.width / (float)ext.height));

    const entt::entity sphere = spawnStaticMesh(w, makeSphereMesh(4, 2.0f), Transform{});
    w.reg.emplace<Name>(sphere, "sphere");
    w.reg.emplace<Material>(sphere, V3{0.25f, 0.25f, 0.25f}, 0.0f, 1.0f);
    w.sim.build();

    applyEnvHdr(w);
}
