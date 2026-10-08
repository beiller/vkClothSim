#include "scenes/scenefile.hpp"

#include "assets.hpp"
#include "json.hpp"
#include "meshgen.hpp"
#include "reflect.hpp"
#include "systems.hpp"
#include "url.hpp"
#include "vk/vkapp.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

namespace {

using nlohmann::json;

void fail(const std::string& path, const std::string& msg) {
    std::printf("scene %s: %s\n", path.c_str(), msg.c_str());
    std::exit(1);
}

V3 v3(const json& a, const char* what) {
    if (!a.is_array() || a.size() != 3)
        fail(what, "expected [x, y, z]");
    return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
}

V2 v2(const json& a, const char* what) {
    if (!a.is_array() || a.size() != 2)
        fail(what, "expected [x, y]");
    return {a[0].get<float>(), a[1].get<float>()};
}

V4 v4(const json& a, const char* what) {
    if (!a.is_array() || a.size() != 4)
        fail(what, "expected [x, y, z, w]");
    return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()};
}

UrlRef urlBind(World& w, const std::string& url, const std::string& path) {
    UrlRef r;
    std::string err;
    if (!resolveUrl(w, url, r, err))
        fail(path, "bind '" + url + "': " + err);
    return r;
}

// ---- editor tab: auto-generated from reflect.hpp tables x ECS contents

void spawnFieldWidget(World& w, entt::entity parent, const char* label, const FieldDesc* f, void* base) {
    char* p = (char*)base + f->offset;
    switch (f->kind) {
    case FieldKind::SliderF:
        spawnWidget(w, parent, UISliderF{label, f->lo, f->hi, "%.2f", [p] { return (float*)p; }});
        break;
    case FieldKind::SliderI: {
        int* ip = (int*)p;
        spawnWidget(w, parent, UISliderI{label, (int)f->lo, (int)f->hi, [ip] { return ip; }});
        break;
    }
    case FieldKind::Checkbox: {
        bool* bp = (bool*)p;
        spawnWidget(w, parent, UICheckbox{label, [bp] { return bp; }});
        break;
    }
    case FieldKind::Color:
        spawnWidget(w, parent, UIColorEdit{label, [p] { return (float*)p; }});
        break;
    case FieldKind::DragV3:
        spawnWidget(w, parent, UIDragV3{label, 0.1f, [p] { return (float*)p; }});
        break;
    }
}

void spawnEditorUi(World& w, entt::entity tab) {
    std::vector<std::pair<entt::entity, std::string>> named;
    for (auto [e, n] : w.reg.view<Name>().each())
        named.push_back({e, n.id});
    std::sort(named.begin(), named.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [e, name] : named) {
        auto comps = refl::entityFields(w.reg, e);
        if (comps.empty())
            continue;
        entt::entity sec = spawnWidget(w, tab, UISection{name, true});
        for (auto& [t, b] : comps)
            for (const auto& f : t->fields) {
                char label[128];
                if (comps.size() > 1)
                    std::snprintf(label, sizeof label, "%s.%s", t->name, f.name);
                else
                    std::snprintf(label, sizeof label, "%s", f.name);
                spawnFieldWidget(w, sec, label, &f, (void*)b);
            }
    }
    for (const auto& r : refl::roots(w)) {
        entt::entity sec = spawnWidget(w, tab, UISection{r.type->name, true});
        for (const auto& f : r.type->fields)
            spawnFieldWidget(w, sec, f.name, &f, r.base);
    }
}

// ---- scene tab: explicit widgets from JSON, bound by URL

void spawnJsonWidgets(World& w, entt::entity parent, const json& arr, const std::string& path) {
    for (const auto& W : arr) {
        const auto type = W.at("type").get<std::string>();
        const auto label = W.value("label", std::string(""));
        if (type == "section") {
            entt::entity sec = spawnWidget(w, parent, UISection{label, W.value("collapsible", true)});
            if (W.contains("children"))
                spawnJsonWidgets(w, sec, W["children"], path);
        } else if (type == "slider-f") {
            const UrlRef r = urlBind(w, W.at("bind").get<std::string>(), path);
            float* p = (float*)r.base;
            spawnWidget(w, parent, UISliderF{label, W.value("lo", r.field->lo), W.value("hi", r.field->hi),
                                              W.value("fmt", std::string("%.2f")), [p] { return p; }});
        } else if (type == "slider-i") {
            const UrlRef r = urlBind(w, W.at("bind").get<std::string>(), path);
            int* p = (int*)r.base;
            spawnWidget(w, parent, UISliderI{label, (int)W.value("lo", (int)r.field->lo),
                                              (int)W.value("hi", (int)r.field->hi), [p] { return p; }});
        } else if (type == "checkbox") {
            const UrlRef r = urlBind(w, W.at("bind").get<std::string>(), path);
            bool* p = (bool*)r.base;
            spawnWidget(w, parent, UICheckbox{label, [p] { return p; }});
        } else if (type == "color") {
            const UrlRef r = urlBind(w, W.at("bind").get<std::string>(), path);
            float* p = (float*)r.base;
            spawnWidget(w, parent, UIColorEdit{label, [p] { return p; }});
        } else if (type == "drag-v3") {
            const UrlRef r = urlBind(w, W.at("bind").get<std::string>(), path);
            float* p = (float*)r.base;
            spawnWidget(w, parent, UIDragV3{label, W.value("speed", 0.1f), [p] { return p; }});
        } else if (type == "text") {
            spawnWidget(w, parent, UIText{label});
        } else if (type == "button") {
            const auto action = W.at("action").get<std::string>();
            if (action == "reset-softs")
                spawnWidget(w, parent, UIButton{label, [&w] { resetSofts(w); }});
            else if (action == "reset-params")
                spawnWidget(w, parent, UIButton{label, [&w] {
                    for (auto [entity, sb] : w.reg.view<SoftBodyData>().each()) {
                        sb.params = SimParams{};
                        sb.steps = kDefaultSteps;
                    }
                }});
            else if (action == "demo-window")
                spawnWidget(w, parent, UIButton{label, [&w] { w.showDemo = true; }});
            else
                fail(path, "unknown button action '" + action + "'");
        } else
            fail(path, "unknown widget type '" + type + "'");
    }
}

void buildUi(World& w, const json& j, const std::string& path) {
    const json& ui = j.value("ui", json::object());
    const auto title = ui.value("title", j.value("name", std::string("scene")));
    const V2 size = ui.contains("size") ? v2(ui["size"], (path + " ui.size").c_str()) : V2{320.0f, 720.0f};
    entt::entity win =
        spawnUiWindow(w, UIWindow{title, [] { return V2{20.0f, 20.0f}; }, size, 0, true});
    if (ui.contains("widgets")) {
        entt::entity tab = spawnWidget(w, win, UITab{"scene"});
        spawnJsonWidgets(w, tab, ui["widgets"], path);
    }
    entt::entity tab = spawnWidget(w, win, UITab{"editor"});
    spawnEditorUi(w, tab);
}

struct MatDef {
    V3 color{1, 1, 1};
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::string tex;
};

} // namespace

void createSceneFileWorld(World& w, const std::string& path) {
    std::ifstream in(path);
    if (!in)
        fail(path, "cannot open file");
    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        fail(path, e.what());
    }

    const json& c = j.at("camera");
    const entt::entity cam = spawnCamera(w, v3(c.at("pos"), (path + " camera.pos").c_str()),
                                         v4(c.at("quat"), (path + " camera.quat").c_str()), c.value("fov", 50.0f),
                                         c.value("active", true));
    if (c.contains("name"))
        w.reg.emplace<Name>(cam, c["name"].get<std::string>());
    w.render.envIntensity = j.value("envIntensity", 1.0f);

    const VkExtent2D ext = w.app->extent();
    w.rigid.init();
    w.sim.init(w.app->device(), w.app->pdev());
    w.renderer.init(*w.app, (int)j["objects"].size(), cameraViewProj(w, cam, (float)ext.width / (float)ext.height));

    for (const auto& L : j["lights"]) {
        const auto name = L.at("name").get<std::string>();
        PointLight p;
        if (L.contains("color"))
            p.color = v3(L["color"], (path + " light color").c_str());
        p.intensity = L.value("intensity", 25.0f);
        p.radius = L.value("radius", 0.25f);
        p.on = L.value("on", 1.0f);
        p.shadowNear = L.value("shadowNear", 0.01f);
        p.shadowFar = L.value("shadowFar", 30.0f);
        p.shadowNormalBias = L.value("shadowNormalBias", 0.05f);
        p.shadowBiasBase = L.value("shadowBiasBase", 0.3f);
        p.shadowBiasSlope = L.value("shadowBiasSlope", 0.3f);
        p.shadowSearchScale = L.value("shadowSearchScale", 2.0f);
        p.shadowMaxRadius = L.value("shadowMaxRadius", 16.0f);
        spawnLight(w, v3(L.at("pos"), (path + " light pos").c_str()), p, name.c_str());
    }

    std::map<std::string, MatDef> mats;
    for (const auto& m : j.value("materials", json::array())) {
        const auto name = m.at("name").get<std::string>();
        MatDef d;
        if (m.contains("baseColor"))
            d.color = v3(m["baseColor"], (path + " material " + name).c_str());
        d.metallic = m.value("metallic", 0.0f);
        d.roughness = m.value("roughness", 0.5f);
        if (m.contains("texture"))
            d.tex = m["texture"].get<std::string>();
        mats[name] = d;
    }

    for (const auto& o : j["objects"]) {
        const auto name = o.at("name").get<std::string>();
        const json& mj = o.at("mesh");
        const auto type = mj.at("type").get<std::string>();
        Mesh mesh;
        if (type == "ground")
            mesh = makeGroundMesh(mj.value("size", 40.0f));
        else if (type == "sphere")
            mesh = makeSphereMesh(mj.value("subdiv", 4), mj.at("radius").get<float>());
        else
            fail(path, "object '" + name + "': unknown mesh type '" + type + "'");

        Transform t{};
        if (o.contains("pos"))
            t.pos = v3(o["pos"], (path + " object " + name + " pos").c_str());
        if (o.contains("quat"))
            t.quat = v4(o["quat"], (path + " object " + name + " quat").c_str());

        const entt::entity e = spawnStaticMesh(w, mesh, t);
        w.reg.emplace<Name>(e, name);

        const auto matName = o.at("material").get<std::string>();
        auto it = mats.find(matName);
        if (it == mats.end())
            fail(path, "object '" + name + "': unknown material '" + matName + "'");
        const MatDef& d = it->second;
        w.reg.emplace<Material>(e, d.color, d.metallic, d.roughness);
        if (!d.tex.empty()) {
            TextureData td;
            if (!loadTexture(exeDir() + "/../assets/" + d.tex, td))
                loadTexture("assets/" + d.tex, td);
            if (td.rgba.empty())
                fail(path, "material '" + matName + "': cannot load texture '" + d.tex + "'");
            const int tex = w.renderer.addTexture(td.rgba.data(), (uint32_t)td.w, (uint32_t)td.h);
            w.renderer.setTexture(w.reg.get<Renderable>(e).inst, 0, tex);
        }
    }

    if (j.contains("set")) {
        for (auto& [k, v] : j["set"].items()) {
            float vals[3];
            int n = v.is_array() ? (int)v.size() : 1;
            if (n > 3)
                fail(path, "set '" + k + "': too many values");
            if (v.is_array())
                for (int i = 0; i < n; ++i)
                    vals[i] = v[(size_t)i].get<float>();
            else
                vals[0] = v.get<float>();
            std::string err;
            if (!sceneSet(w, k, vals, n, err))
                fail(path, "set '" + k + "': " + err);
        }
    }

    w.sim.build();
    if (j.contains("env")) {
        const auto env = j["env"].get<std::string>();
        HdrData hdr;
        if (!loadHdr(exeDir() + "/../assets/" + env, hdr))
            loadHdr("assets/" + env, hdr);
        if (hdr.rgb.empty())
            fail(path, "cannot load env '" + env + "'");
        w.renderer.setEnvironment(*w.app, hdr.rgb.data(), (uint32_t)hdr.w, (uint32_t)hdr.h);
    }
    buildUi(w, j, path);
}
