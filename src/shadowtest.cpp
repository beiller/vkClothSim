#include "shadowtest.hpp"

#include "assets.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <map>

namespace {

struct IcoSphere {
    std::vector<V3> verts;
    std::vector<uint32_t> tris;
};

IcoSphere makeIcoSphere(int subdiv) {
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<V3> verts{{-1, t, 0},  {1, t, 0},  {-1, -t, 0}, {1, -t, 0}, {0, -1, t},  {0, 1, t},
                          {0, -1, -t}, {0, 1, -t}, {t, 0, -t},  {t, 0, 1},  {-t, 0, -t}, {-t, 0, 1}};
    for (auto& v : verts)
        v = vScale(v, 1.0f / vLen(v));
    std::vector<uint32_t> tris{0, 11, 5,  0, 5,  1, 0, 1, 7, 0, 7,  10, 0, 10, 11, 1, 5, 9, 5, 11,
                               4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3,  9,  4, 3,  4,  2, 3, 2, 6, 3,
                               6, 8,  3,  8, 9,  4, 9, 5, 2, 4, 11, 6,  2, 10, 8,  6, 7, 9, 8, 1};
    for (int s = 0; s < subdiv; ++s) {
        std::map<uint64_t, uint32_t> mid;
        auto midOf = [&](uint32_t a, uint32_t b) {
            const uint64_t key = (a < b) ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
            auto it = mid.find(key);
            if (it != mid.end())
                return it->second;
            V3 m = vScale(vAdd(verts[a], verts[b]), 0.5f);
            m = vScale(m, 1.0f / vLen(m));
            const uint32_t idx = (uint32_t)verts.size();
            verts.push_back(m);
            mid[key] = idx;
            return idx;
        };
        std::vector<uint32_t> next;
        next.reserve(tris.size() * 4);
        for (size_t f = 0; f < tris.size(); f += 3) {
            const uint32_t a = tris[f], b = tris[f + 1], c = tris[f + 2];
            const uint32_t ab = midOf(a, b), bc = midOf(b, c), ca = midOf(c, a);
            for (uint32_t v : {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca})
                next.push_back(v);
        }
        tris = std::move(next);
    }
    return {verts, tris};
}

Mesh makeSphereMesh(int subdiv, float radius, const V3& center) {
    IcoSphere ico = makeIcoSphere(subdiv);
    const int n = (int)ico.verts.size();
    Mesh out;
    out.pos.assign((size_t)3 * n, 0.0f);
    out.nrm.assign((size_t)3 * n, 0.0f);
    out.uv.assign((size_t)2 * n, 0.0f);
    out.indices = ico.tris;
    for (int i = 0; i < n; ++i) {
        const V3 d = ico.verts[i];
        vStore(out.pos.data(), i, vAdd(center, vScale(d, radius)));
        vStore(out.nrm.data(), i, d);
        out.uv[2 * i] = 0.5f + std::atan2f(d.z, d.x) / (2.0f * kPi);
        out.uv[2 * i + 1] = 0.5f + std::asinf(d.y) / kPi;
    }
    return out;
}

Mesh makeGroundMesh(float span) {
    Mesh m;
    const V3 corners[4] = {{-span, 0.0f, -span}, {span, 0.0f, -span}, {span, 0.0f, span}, {-span, 0.0f, span}};
    const V3 up{0.0f, 1.0f, 0.0f};
    m.pos.assign(3 * 4, 0.0f);
    m.nrm.assign(3 * 4, 0.0f);
    m.uv.assign(2 * 4, 0.0f);
    for (int i = 0; i < 4; ++i) {
        vStore(m.pos.data(), i, corners[i]);
        vStore(m.nrm.data(), i, up);
        m.uv[2 * i] = (corners[i].x + span) / (2.0f * span);
        m.uv[2 * i + 1] = (corners[i].z + span) / (2.0f * span);
    }
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}

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
    ImGui::SeparatorText("tone");
    ImGui::SliderFloat("exposure", &w.ui.exposure, 0.1f, 3.0f, "%.2f");
    ImGui::SliderFloat("hdri intensity", &w.ui.envIntensity, 0.0f, 3.0f, "%.2f");
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

    HdrData hdr;
    if (!loadHdr(exeDir() + "/../assets/fly-studio-03_1K.exr", hdr))
        loadHdr("assets/fly-studio-03_1K.exr", hdr);
    if (!hdr.rgb.empty())
        w.renderer.setEnvironment(*w.app, hdr.rgb.data(), (uint32_t)hdr.w, (uint32_t)hdr.h);
}
