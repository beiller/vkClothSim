#include "scenes/hdri.hpp"

#include "assets.hpp"
#include "meshgen.hpp"
#include "scenes/scene_common.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdint>
#include <imgui.h>

namespace {

void buildHdriUi(World& w, entt::entity sphere) {
    entt::entity win =
        spawnUiWindow(w, UIWindow{"hdri", [] { return V2{20.0f, 20.0f}; }, V2{320.0f, 680.0f}, 0, true});
    Material& m = w.reg.get<Material>(sphere);
    spawnWidget(w, win, UIColorEdit{"base color", [&m] { return &m.baseColor.x; }});
    spawnWidget(w, win, UISliderF{"metallic", 0.0f, 1.0f, "%.2f", [&m] { return &m.metallic; }});
    spawnWidget(w, win, UISliderF{"roughness", 0.0f, 1.0f, "%.2f", [&m] { return &m.roughness; }});
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

void createHdriWorld(World& w) {
    const entt::entity cam = spawnCamera(w, V3{0.0f, 0.0f, 6.0f}, V4{0.0f, 0.0f, 0.0f, 1.0f}, 50.0f);
    spawnLight(w, V3{2.0f, 2.0f, 3.0f}, PointLight{V3{1.0f, 1.0f, 1.0f}, 20.0f, 0.15f, 1.0f}, "light");

    const VkExtent2D ext = w.app->extent();
    w.rigid.init();
    w.sim.init(w.app->device(), w.app->pdev());
    w.renderer.init(*w.app, 1, cameraViewProj(w, cam, (float)ext.width / (float)ext.height));

    Transform st{};
    st.pos = V3{0.0f, 0.0f, 0.0f};
    const entt::entity sphere = spawnStaticMesh(w, makeSphereMesh(4, 2.0f), st);
    w.reg.emplace<Name>(sphere, "sphere");
    w.reg.emplace<Material>(sphere, V3{0.25f, 0.25f, 0.25f}, 0.0f, 1.0f);
    w.sim.build();

    applyEnvHdr(w);
    buildHdriUi(w, sphere);
}
