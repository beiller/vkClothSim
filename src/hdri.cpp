#include "hdri.hpp"

#include "assets.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <map>
#include <vector>

namespace {

struct IcoSphere {
    std::vector<V3> verts;
    std::vector<uint32_t> tris;
};

IcoSphere makeIcoSphere(int subdiv) {
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<V3> verts{{-1, t, 0},  {1, t, 0},  {-1, -t, 0}, {1, -t, 0}, {0, -1, t},  {0, 1, t},
                          {0, -1, -t}, {0, 1, -t}, {t, 0, -1},  {t, 0, 1},  {-t, 0, -1}, {-t, 0, 1}};
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
            auto idx = (uint32_t)verts.size();
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

Mesh makeSphereMesh(int subdiv, float radius) {
    IcoSphere ico = makeIcoSphere(subdiv);
    const int n = (int)ico.verts.size();
    Mesh out;
    out.pos.assign((size_t)3 * n, 0.0f);
    out.nrm.assign((size_t)3 * n, 0.0f);
    out.uv.assign((size_t)2 * n, 0.0f);
    out.indices = ico.tris;
    for (int i = 0; i < n; ++i) {
        const V3 d = ico.verts[i];
        vStore(out.pos.data(), i, vScale(d, radius));
        vStore(out.nrm.data(), i, d);
        out.uv[2 * i] = 0.5f + std::atan2f(d.z, d.x) / (2.0f * kPi);
        out.uv[2 * i + 1] = 0.5f + std::asinf(d.y) / kPi;
    }
    return out;
}

void drawHdriUi(World& w) {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, 380), ImGuiCond_Always);
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
    ImGui::SeparatorText("tone");
    ImGui::SliderFloat("exposure", &w.ui.exposure, 0.1f, 3.0f, "%.2f");
    ImGui::End();
}

} // namespace

void createHdriWorld(World& w) {
    w.camera.position = {0.0f, 0.0f, 6.0f};
    w.camera.rotation = V4{0.0f, 0.0f, 0.0f, 1.0f};
    w.drawUi = drawHdriUi;

    const VkExtent2D ext = w.app->extent();
    w.rigid.init();
    w.sim.init(w.app->device(), w.app->pdev());
    w.renderer.init(*w.app, 1, w.camera.viewProj((float)ext.width / (float)ext.height));

    const entt::entity sphere = spawnStaticMesh(w, makeSphereMesh(4, 2.0f), Transform{});
    w.reg.emplace<Name>(sphere, "sphere");
    w.reg.emplace<Material>(sphere, V3{0.25f, 0.25f, 0.25f}, 0.0f, 1.0f);
    w.sim.build();

    HdrData hdr;
    if (!loadHdr(exeDir() + "/../assets/fly-studio-03_1K.exr", hdr))
        loadHdr("assets/fly-studio-03_1K.exr", hdr);
    if (!hdr.rgb.empty())
        w.renderer.setEnvironment(*w.app, hdr.rgb.data(), (uint32_t)hdr.w, (uint32_t)hdr.h);
}
