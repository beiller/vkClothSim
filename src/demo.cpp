#include "demo.hpp"

#include "app/rigid.hpp"
#include "assets.hpp"
#include "capsule.hpp"
#include "meshgen.hpp"
#include "scene_common.hpp"
#include "systems.hpp"
#include "vk/vkapp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <map>
#include <random>

namespace {

constexpr int kCW = 64, kCH = 64;
constexpr float kClothSpan = 8.0f;
constexpr float kClothY0 = 10.0f;
constexpr int kBallSubdiv = 2;
constexpr float kBallRadius = 1.5f;
constexpr float kBallY0 = 12.0f;
constexpr int kNCapsules = 50;
constexpr int kNSoftBodies = 2;
constexpr float kClothHoldTime = 3.0f;

constexpr int kCapPhiSegs = 20;
constexpr int kCapYRows = 32;
constexpr uint32_t kCapVPC = (uint32_t)(kCapYRows + 1) * kCapPhiSegs;

RigidBody scatteredBody(std::mt19937& rng, std::uniform_real_distribution<float>& rnd) {
    const float px = (rnd(rng) * 2.0f - 1.0f) * 2.0f;
    const float py = 4.5f + 3.5f * rnd(rng);
    const float pz = (rnd(rng) * 2.0f - 1.0f) * 2.0f;
    const V4 rot = quatMul(quatAxisAngle({0, 1, 0}, rnd(rng) * 2.0f * kPi),
                           quatAxisAngle({1, 0, 0}, (rnd(rng) * 2.0f - 1.0f) * 0.9f));
    return {kCapsule, {px, py, pz}, rot};
}

Mesh makeClothMesh(int CW, int CH, float span, float y0) {
    Mesh out;
    const int CN = CW * CH;
    out.pos.assign((size_t)3 * CN, 0.0f);
    out.nrm.assign((size_t)3 * CN, 0.0f);
    out.uv.assign((size_t)2 * CN, 0.0f);
    for (int gy = 0; gy < CH; ++gy)
        for (int gx = 0; gx < CW; ++gx) {
            int i = gy * CW + gx;
            const float x = -span / 2 + span * (float)gx / (float)(CW - 1);
            const float z = -span / 2 + span * (float)gy / (float)(CH - 1);
            vStore(out.pos.data(), i, {x, y0, z});
            out.nrm[3 * i + 1] = 1.0f;
            out.uv[2 * i] = (float)gx / (float)(CW - 1);
            out.uv[2 * i + 1] = (float)gy / (float)(CH - 1);
        }
    for (int gy = 0; gy < CH - 1; ++gy)
        for (int gx = 0; gx < CW - 1; ++gx) {
            auto a = (uint32_t)(gy * CW + gx);
            out.indices.push_back(a);
            out.indices.push_back(a + (uint32_t)CW);
            out.indices.push_back(a + 1);
            out.indices.push_back(a + 1);
            out.indices.push_back(a + (uint32_t)CW);
            out.indices.push_back(a + (uint32_t)CW + 1);
        }
    return out;
}

std::vector<sim::Constraint> makeClothCons(int CW, int CH, float span) {
    std::vector<sim::Constraint> cons;
    const int CN = CW * CH;
    const float spacing = span / (float)(CW - 1);
    const float diag = spacing * kSqrt2;
    for (int i = 0; i < CN; ++i) {
        int gx = i % CW, gy = i / CW;
        if (gx < CW - 1)
            cons.push_back({i, i + 1, spacing, 1.0f});
        if (gy < CH - 1)
            cons.push_back({i, i + CW, spacing, 1.0f});
        if (gx < CW - 1 && gy < CH - 1)
            cons.push_back({i, i + CW + 1, diag, 1.0f});
        if (gx > 0 && gy < CH - 1)
            cons.push_back({i, i + CW - 1, diag, 1.0f});
    }
    return cons;
}

void addBallEdges(const Mesh& mesh, std::vector<sim::Constraint>& cons, const std::vector<uint32_t>& tris) {
    const float* pos = mesh.pos.data();
    std::map<uint64_t, bool> seen;
    for (size_t f = 0; f < tris.size(); f += 3) {
        const uint32_t e[3][2] = {{tris[f], tris[f + 1]}, {tris[f + 1], tris[f + 2]}, {tris[f + 2], tris[f]}};
        for (const auto& p : e) {
            const int lo = p[0] < p[1] ? (int)p[0] : (int)p[1];
            const int hi = p[0] < p[1] ? (int)p[1] : (int)p[0];
            const uint64_t key = ((uint64_t)lo << 32) | (uint32_t)hi;
            if (seen.contains(key))
                continue;
            seen[key] = true;
            cons.push_back({lo, hi, vLen(vSub(vAt(pos, lo), vAt(pos, hi))), 0.5f});
        }
    }
}

void addAntipodalTies(const Mesh& mesh, std::vector<sim::Constraint>& cons) {
    const int n = mesh.vertexCount();
    const float* pos = mesh.pos.data();
    V3 c{0, 0, 0};
    for (int i = 0; i < n; ++i)
        c = vAdd(c, vAt(pos, i));
    c = vScale(c, 1.0f / (float)n);
    std::vector<V3> dir(n);
    for (int i = 0; i < n; ++i)
        dir[i] = vNorm(vSub(vAt(pos, i), c));
    for (int i = 0; i < n; ++i) {
        int partner = -1;
        float bestDot = 1e30f;
        for (int j = 0; j < n; ++j) {
            if (j == i)
                continue;
            const float d = vDot(dir[j], dir[i]);
            if (d < bestDot) {
                bestDot = d;
                partner = j;
            }
        }
        if (partner <= i)
            continue;
        cons.push_back({i, partner, vLen(vSub(vAt(pos, i), vAt(pos, partner))), 0.5f});
    }
}

std::vector<sim::Constraint> makeBallCons(const Mesh& mesh) {
    std::vector<sim::Constraint> cons;
    addBallEdges(mesh, cons, mesh.indices);
    addAntipodalTies(mesh, cons);
    return cons;
}

Mesh makeCapsuleMesh(const CapsuleParams& shape, int nCaps) {
    Mesh m;
    const size_t vcount = (size_t)nCaps * kCapVPC;
    m.pos.assign(3 * vcount, 0.0f);
    m.nrm.assign(3 * vcount, 0.0f);
    m.uv.assign(2 * vcount, 0.0f);
    const int S = kCapPhiSegs, M = kCapYRows;
    const int VPC = (int)kCapVPC;
    const float R = shape.radius, H = shape.halfLen;
    const float top = H + R, bot = -H - R, twoPi = 2.0f * kPi;
    for (int ci = 0; ci < nCaps; ++ci) {
        const uint32_t capBase = (uint32_t)ci * VPC;
        for (int i = 0; i <= M; ++i) {
            const float y = top - (float)i / M * (top - bot);
            float dy = 0.0f;
            if (y >= H)
                dy = y - H;
            else if (y <= -H)
                dy = y + H;
            float r, dr;
            if (dy == 0.0f) {
                r = R;
                dr = 0.0f;
            } else {
                r = std::sqrt(std::max(0.0f, R * R - dy * dy));
                dr = (r > 1e-5f) ? -dy / r : 0.0f;
            }
            for (int j = 0; j < S; ++j) {
                const float phi = (float)j / S * twoPi;
                const float cp = std::cos(phi), sp = std::sin(phi);
                const int v = (int)(capBase + (size_t)i * S + (size_t)j);
                vStore(m.pos.data(), v, {r * cp, y, r * sp});
                vStore(m.nrm.data(), v, {cp, -dr, sp});
                m.uv[2 * v] = (float)j / S;
                m.uv[2 * v + 1] = (float)i / M;
            }
        }
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < S; ++j) {
                const auto jn = (uint32_t)((j + 1) % S);
                const uint32_t a = capBase + (uint32_t)i * S + (uint32_t)j;
                const uint32_t b = capBase + (uint32_t)i * S + jn;
                const uint32_t c = capBase + (uint32_t)(i + 1) * S + (uint32_t)j;
                const uint32_t d = capBase + (uint32_t)(i + 1) * S + jn;
                m.indices.push_back(a);
                m.indices.push_back(b);
                m.indices.push_back(d);
                m.indices.push_back(a);
                m.indices.push_back(d);
                m.indices.push_back(c);
            }
    }
    return m;
}

void drawDemoUi(World& w) {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, 800), ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Appearing);
    ImGui::Begin("3dsim");
    ImGui::ColorEdit3("background", w.ui.bgColor);
    ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
    ImGui::Text("sim: GPU (cloth + ball)");
    for (auto [entity, sb] : w.reg.view<SoftBodyData>().each()) {
        const char* label = "soft body";
        if (const auto* n = w.reg.try_get<Name>(entity))
            label = n->id.c_str();
        ImGui::PushID((int)entity);
        if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("mass", &sb.params.mass, 0.1f, 10.0f, "%.2f");
            ImGui::SliderFloat("damping", &sb.params.damping, 0.90f, 1.00f, "%.3f");
            ImGui::SliderInt("passes", &sb.params.passes, 1, 16);
            ImGui::SliderFloat("stiffness", &sb.params.stiffness, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("tension", &sb.params.tension, 0.5f, 1.5f, "%.2f");
            ImGui::SliderFloat("friction", &sb.params.friction, 0.0f, 50.0f, "%.2f");
            ImGui::SliderInt("steps", &sb.steps, 1, 32);
        }
        ImGui::PopID();
    }
    ImGui::SeparatorText("jolt iterations");
    ImGui::SliderInt("velocity", &w.ui.joltIters, 1, 64);
    ImGui::SeparatorText("camera");
    ImGui::SliderFloat("fov", &w.camera.fovDeg, 10.0f, 120.0f, "%.0f");
    ImGui::SliderFloat("near", &w.camera.nearP, 0.01f, 5.0f, "%.3f");
    ImGui::SliderFloat("far", &w.camera.farP, 10.0f, 1000.0f, "%.0f");
    drawToneSection(w);
    drawLightSection(w);
    ImGui::SeparatorText("materials");
    std::map<std::string, std::vector<entt::entity>> matByName;
    for (auto [entity, mat] : w.reg.view<Material>().each()) {
        const auto* name = w.reg.try_get<Name>(entity);
        (void)mat;
        matByName[name ? name->id : "material"].push_back(entity);
    }
    for (auto& [name, ents] : matByName) {
        Material m = w.reg.get<Material>(ents[0]);
        ImGui::PushID(name.c_str());
        if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("base color", &m.baseColor.x);
            ImGui::SliderFloat("metallic", &m.metallic, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("roughness", &m.roughness, 0.0f, 1.0f, "%.2f");
        }
        ImGui::PopID();
        for (entt::entity e : ents)
            w.reg.get<Material>(e) = m;
    }
    if (ImGui::Button("reset params"))
        for (auto [entity, sb] : w.reg.view<SoftBodyData>().each()) {
            sb.params = SimParams{};
            sb.steps = kDefaultSteps;
        }
    if (ImGui::Button("reset (R)"))
        resetSofts(w);
    if (ImGui::Button("demo window"))
        w.ui.showDemo = true;
    ImGui::End();
    if (w.ui.showDemo)
        ImGui::ShowDemoWindow(&w.ui.showDemo);
}

} // namespace

void createDemoWorld(World& w) {
    w.camera.position = {0.0f, 9.0f, 14.0f};
    w.camera.rotation = quatAxisAngle({1.0f, 0.0f, 0.0f}, -std::atan2f(6.0f, 14.0f));
    w.drawUi = drawDemoUi;
    spawnLight(w, V3{0.0f, 12.0f, 0.0f}, PointLight{V3{1.0f, 0.95f, 0.90f}, 120.0f, 0.5f, 1.0f, 0.01f, 30.0f}, "key light");
    spawnLight(w, V3{-9.0f, 6.0f, -7.0f}, PointLight{V3{0.4f, 0.6f, 1.0f}, 60.0f, 0.5f, 1.0f}, "rim light");
    w.ui.envIntensity = 0.0f;

    const VkExtent2D ext = w.app->extent();
    w.rigid.init();
    w.sim.init(w.app->device(), w.app->pdev());
    w.renderer.init(*w.app, kNCapsules + kNSoftBodies + 1, w.camera.viewProj((float)ext.width / (float)ext.height));

    const entt::entity ground = spawnStaticMesh(w, makeGroundMesh(55.0f), Transform{});
    w.reg.emplace<Name>(ground, "ground");
    w.reg.emplace<Material>(ground, V3{0.19f, 0.21f, 0.17f}, 0.0f, 0.85f);
    const int capGeom = w.renderer.addMesh(makeCapsuleMesh(kCapsule, 1)).geom;
    std::mt19937 rng(12345); // NOLINT(bugprone-random-generator-seed)
    std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
    for (int i = 0; i < kNCapsules; ++i) {
        const entt::entity cap = spawnCapsule(w, scatteredBody(rng, rnd), capGeom);
        w.reg.emplace<Name>(cap, "capsule");
        w.reg.emplace<Material>(cap, V3{0.85f, 0.35f, 0.30f}, 0.9f, 0.25f);
    }

    const entt::entity cloth =
        spawnSoftBody(w, makeClothMesh(kCW, kCH, kClothSpan, kClothY0), makeClothCons(kCW, kCH, kClothSpan), 1);
    w.reg.emplace<Name>(cloth, "cloth");
    w.reg.emplace<PinHold>(cloth, kClothHoldTime);
    w.reg.emplace<Material>(cloth, V3{1.0f, 1.0f, 1.0f}, 0.0f, 0.8f);
    TextureData plaid;
    if (!loadTexture(exeDir() + "/../assets/cloth_albedo.png", plaid))
        loadTexture("assets/cloth_albedo.png", plaid);
    if (!plaid.rgba.empty())
        w.renderer.setTexture(w.reg.get<Renderable>(cloth).inst, 0,
                              w.renderer.addTexture(plaid.rgba.data(), (uint32_t)plaid.w, (uint32_t)plaid.h));
    const Mesh ballMesh = makeSphereMesh(kBallSubdiv, kBallRadius, V3{0.0f, kBallY0, 0.0f});
    const entt::entity ball = spawnSoftBody(w, ballMesh, makeBallCons(ballMesh), 0);
    w.reg.emplace<Name>(ball, "ball");
    w.reg.get<SoftBodyData>(ball).params.mass = 2.0f;
    w.reg.emplace<Material>(ball, V3{0.22f, 0.62f, 0.60f}, 0.0f, 0.4f);
    w.sim.build();

    applyEnvHdr(w);
}
