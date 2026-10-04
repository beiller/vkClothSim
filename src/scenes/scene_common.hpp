#pragma once
#include "assets.hpp"
#include "systems.hpp"
#include "world.hpp"
#include <imgui.h>

inline entt::entity spawnToneSection(World& w, entt::entity win) {
    entt::entity sec = spawnWidget(w, win, UISection{"tone", false});
    spawnWidget(w, sec, UISliderF{"exposure", 0.1f, 3.0f, "%.2f", [&w] { return &w.render.exposure; }});
    spawnWidget(w, sec, UISliderF{"hdri intensity", 0.0f, 3.0f, "%.2f", [&w] { return &w.render.envIntensity; }});
    return sec;
}

// one collapsible sub-section per PointLight entity, spawned at world creation
inline entt::entity spawnLightSection(World& w, entt::entity win) {
    entt::entity sec = spawnWidget(w, win, UISection{"lights", false});
    for (auto [entity, t, p] : w.reg.view<Transform, PointLight>().each()) {
        const auto* name = w.reg.try_get<Name>(entity);
        entt::entity sub = spawnWidget(w, sec, UISection{name ? name->id : "light", true});
        spawnWidget(w, sub, UISliderF{"on", 0.0f, 1.0f, "%.0f", [&p] { return &p.on; }});
        spawnWidget(w, sub, UIDragV3{"pos", 0.1f, [&t] { return &t.pos.x; }});
        spawnWidget(w, sub, UIColorEdit{"color", [&p] { return &p.color.x; }});
        spawnWidget(w, sub, UISliderF{"intensity", 0.0f, 1000.0f, "%.0f", [&p] { return &p.intensity; }});
        spawnWidget(w, sub, UISliderF{"radius", 0.0f, 2.0f, "%.2f", [&p] { return &p.radius; }});
        spawnWidget(w, sub, UISliderF{"shadow near", 0.001f, 1.0f, "%.3f", [&p] { return &p.shadowNear; }});
        spawnWidget(w, sub, UISliderF{"shadow far", 1.0f, 100.0f, "%.1f", [&p] { return &p.shadowFar; }});
        entt::entity bias = spawnWidget(w, sub, UISection{"shadow bias", false});
        spawnWidget(w, bias, UISliderF{"normal", 0.0f, 1.0f, "%.3f", [&p] { return &p.shadowNormalBias; }});
        spawnWidget(w, bias, UISliderF{"base", 0.0f, 2.0f, "%.3f", [&p] { return &p.shadowBiasBase; }});
        spawnWidget(w, bias, UISliderF{"slope", 0.0f, 4.0f, "%.3f", [&p] { return &p.shadowBiasSlope; }});
        spawnWidget(w, bias, UISliderF{"search", 0.5f, 16.0f, "%.2f", [&p] { return &p.shadowSearchScale; }});
        spawnWidget(w, bias, UISliderF{"max radius", 1.0f, 64.0f, "%.1f", [&p] { return &p.shadowMaxRadius; }});
    }
    return sec;
}

inline void applyEnvHdr(World& w) {
    HdrData hdr;
    if (!loadHdr(exeDir() + "/../assets/fly-studio-03_1K.exr", hdr))
        loadHdr("assets/fly-studio-03_1K.exr", hdr);
    if (!hdr.rgb.empty())
        w.renderer.setEnvironment(*w.app, hdr.rgb.data(), (uint32_t)hdr.w, (uint32_t)hdr.h);
}
