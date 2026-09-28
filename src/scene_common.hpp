#pragma once
#include "assets.hpp"
#include "world.hpp"
#include <imgui.h>

inline void drawToneSection(World& w) {
    ImGui::SeparatorText("tone");
    ImGui::SliderFloat("exposure", &w.ui.exposure, 0.1f, 3.0f, "%.2f");
    ImGui::SliderFloat("hdri intensity", &w.ui.envIntensity, 0.0f, 3.0f, "%.2f");
}

inline void drawLightSection(World& w) {
    ImGui::SeparatorText("lights");
    for (auto [entity, t, p] : w.reg.view<Transform, PointLight>().each()) {
        const auto* name = w.reg.try_get<Name>(entity);
        const char* label = name ? name->id.c_str() : "light";
        ImGui::PushID((int)entity);
        if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("on", &p.on, 0.0f, 1.0f, "%.0f");
            ImGui::DragFloat3("pos", &t.pos.x, 0.1f);
            ImGui::ColorEdit3("color", &p.color.x);
            ImGui::SliderFloat("intensity", &p.intensity, 0.0f, 1000.0f, "%.0f");
            ImGui::SliderFloat("radius", &p.radius, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("shadow near", &p.shadowNear, 0.001f, 1.0f, "%.3f");
            ImGui::SliderFloat("shadow far", &p.shadowFar, 1.0f, 100.0f, "%.1f");
            ImGui::SeparatorText("shadow bias");
            ImGui::SliderFloat("normal", &p.shadowNormalBias, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("base", &p.shadowBiasBase, 0.0f, 2.0f, "%.3f");
            ImGui::SliderFloat("slope", &p.shadowBiasSlope, 0.0f, 4.0f, "%.3f");
            ImGui::SliderFloat("search", &p.shadowSearchScale, 0.5f, 16.0f, "%.2f");
            ImGui::SliderFloat("max radius", &p.shadowMaxRadius, 1.0f, 64.0f, "%.1f");
        }
        ImGui::PopID();
    }
}

inline void applyEnvHdr(World& w) {
    HdrData hdr;
    if (!loadHdr(exeDir() + "/../assets/fly-studio-03_1K.exr", hdr))
        loadHdr("assets/fly-studio-03_1K.exr", hdr);
    if (!hdr.rgb.empty())
        w.renderer.setEnvironment(*w.app, hdr.rgb.data(), (uint32_t)hdr.w, (uint32_t)hdr.h);
}
