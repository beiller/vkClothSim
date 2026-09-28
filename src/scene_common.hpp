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
    ImGui::SeparatorText("point light");
    ImGui::SliderFloat("on", &w.ui.light.on, 0.0f, 1.0f, "%.0f");
    ImGui::DragFloat3("pos", &w.ui.light.pos.x, 0.1f);
    ImGui::ColorEdit3("color", &w.ui.light.color.x);
    ImGui::SliderFloat("intensity", &w.ui.light.intensity, 0.0f, 1000.0f, "%.0f");
    ImGui::SliderFloat("radius", &w.ui.light.radius, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("shadow near", &w.ui.light.shadowNear, 0.001f, 1.0f, "%.3f");
    ImGui::SliderFloat("shadow far", &w.ui.light.shadowFar, 1.0f, 100.0f, "%.1f");
    ImGui::SeparatorText("shadow bias");
    ImGui::SliderFloat("normal", &w.ui.light.shadowNormalBias, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("base", &w.ui.light.shadowBiasBase, 0.0f, 2.0f, "%.3f");
    ImGui::SliderFloat("slope", &w.ui.light.shadowBiasSlope, 0.0f, 4.0f, "%.3f");
    ImGui::SliderFloat("search", &w.ui.light.shadowSearchScale, 0.5f, 16.0f, "%.2f");
    ImGui::SliderFloat("max radius", &w.ui.light.shadowMaxRadius, 1.0f, 64.0f, "%.1f");
}

inline void applyEnvHdr(World& w) {
    HdrData hdr;
    if (!loadHdr(exeDir() + "/../assets/fly-studio-03_1K.exr", hdr))
        loadHdr("assets/fly-studio-03_1K.exr", hdr);
    if (!hdr.rgb.empty())
        w.renderer.setEnvironment(*w.app, hdr.rgb.data(), (uint32_t)hdr.w, (uint32_t)hdr.h);
}
