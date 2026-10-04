#include "scenes/shadowtest.hpp"

#include "assets.hpp"
#include "meshgen.hpp"
#include "scenes/scene_common.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdint>
#include <imgui.h>

namespace {

void buildShadowUi(World& w) {
    entt::entity win =
        spawnUiWindow(w, UIWindow{"shadow test", [] { return V2{20.0f, 20.0f}; }, V2{320.0f, 720.0f}, 0, true});
    for (auto [entity, mat] : w.reg.view<Material>().each()) {
        const auto* name = w.reg.try_get<Name>(entity);
        if (!name)
            continue;
        (void)mat;
        entt::entity sub = spawnWidget(w, win, UISection{name->id, false});
        Material& m = w.reg.get<Material>(entity);
        spawnWidget(w, sub, UIColorEdit{"base color", [&m] { return &m.baseColor.x; }});
        spawnWidget(w, sub, UISliderF{"metallic", 0.0f, 1.0f, "%.2f", [&m] { return &m.metallic; }});
        spawnWidget(w, sub, UISliderF{"roughness", 0.0f, 1.0f, "%.2f", [&m] { return &m.roughness; }});
    }
    entt::entity camSec = spawnWidget(w, win, UISection{"camera", false});
    const entt::entity cam = findActiveCamera(w);
    if (cam != entt::null) {
        Camera& c = w.reg.get<Camera>(cam);
        spawnWidget(w, camSec, UISliderF{"fov", 10.0f, 120.0f, "%.0f", [&c] { return &c.fovDeg; }});
    }
    spawnToneSection(w, win);
    spawnLightSection(w, win);
}

} // namespace

void createShadowTestWorld(World& w) {
    const entt::entity cam =
        spawnCamera(w, V3{0.0f, 4.0f, 11.0f}, quatAxisAngle({1.0f, 0.0f, 0.0f}, -std::atan2f(2.0f, 11.0f)), 50.0f);
    spawnLight(w, V3{0.0f, 7.0f, 0.0f}, PointLight{V3{1.0f, 0.97f, 0.92f}, 300.0f, 0.5f, 1.0f}, "key light");
    w.render.envIntensity = 0.0f;

    const VkExtent2D ext = w.app->extent();
    w.rigid.init();
    w.sim.init(w.app->device(), w.app->pdev());
    w.renderer.init(*w.app, 2, cameraViewProj(w, cam, (float)ext.width / (float)ext.height));

    const entt::entity ground = spawnStaticMesh(w, makeGroundMesh(40.0f), Transform{});
    w.reg.emplace<Name>(ground, "ground");
    w.reg.emplace<Material>(ground, V3{1.0f, 1.0f, 1.0f}, 0.0f, 0.9f);

    const entt::entity ball = spawnStaticMesh(w, makeSphereMesh(4, 2.0f, V3{0.0f, 3.0f, 0.0f}), Transform{});
    w.reg.emplace<Name>(ball, "ball");
    w.reg.emplace<Material>(ball, V3{0.5f, 0.5f, 0.5f}, 0.0f, 0.35f);

    w.sim.build();

    applyEnvHdr(w);
    buildShadowUi(w);
}
