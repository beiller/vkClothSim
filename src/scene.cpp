#include "scene.hpp"

#include "assets.hpp"
#include "json.hpp"
#include "reflect.hpp"
#include "systems.hpp"
#include "ui.hpp"
#include "url.hpp"
#include "vk/vkapp.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

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

// path in the cwd, or relative to the exe (running from the build dir)
std::string resolveFile(const std::string& p) {
    return std::filesystem::exists(p) ? p : exeDir() + "/../" + p;
}

// ---- editor tab: auto-generated from reflect.hpp tables x ECS contents

void spawnEditorUi(World& w, entt::entity tab) {
    std::vector<std::pair<entt::entity, std::string>> named;
    for (auto [e, n] : w.reg.view<Name>().each())
        named.emplace_back(e, n.id);
    std::ranges::sort(named, [](auto& a, auto& b) { return a.first < b.first; });
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

void spawnJsonWidgets(World& w, entt::entity parent, const json& arr, const std::string& path,
                      std::vector<std::string>& scriptButtons) {
    for (const auto& W : arr) {
        const auto type = W.at("type").get<std::string>();
        const auto label = W.value("label", std::string(""));
        if (type == "section") {
            entt::entity sec = spawnWidget(w, parent, UISection{label, W.value("collapsible", true)});
            if (W.contains("children"))
                spawnJsonWidgets(w, sec, W["children"], path, scriptButtons);
        } else if (type == "slider-f") {
            auto r = urlBind(w, W.at("bind").get<std::string>(), path);
            auto* p = (float*)r.base;
            spawnWidget(w, parent,
                        UISliderF{label, W.value("lo", r.field->lo), W.value("hi", r.field->hi),
                                  W.value("fmt", std::string("%.2f")), [p] { return p; }});
        } else if (type == "slider-i") {
            auto r = urlBind(w, W.at("bind").get<std::string>(), path);
            auto* p = (int*)r.base;
            spawnWidget(w, parent,
                        UISliderI{label, (int)W.value("lo", (int)r.field->lo), (int)W.value("hi", (int)r.field->hi),
                                  [p] { return p; }});
        } else if (type == "checkbox") {
            auto r = urlBind(w, W.at("bind").get<std::string>(), path);
            auto* p = (bool*)r.base;
            spawnWidget(w, parent, UICheckbox{label, [p] { return p; }});
        } else if (type == "color") {
            auto r = urlBind(w, W.at("bind").get<std::string>(), path);
            auto* p = (float*)r.base;
            spawnWidget(w, parent, UIColorEdit{label, [p] { return p; }});
        } else if (type == "drag-v3") {
            auto r = urlBind(w, W.at("bind").get<std::string>(), path);
            auto* p = (float*)r.base;
            spawnWidget(w, parent, UIDragV3{label, W.value("speed", 0.1f), [p] { return p; }});
        } else if (type == "text") {
            spawnWidget(w, parent, UIText{label});
        } else if (type == "button") {
            const auto action = W.at("action").get<std::string>();
            if (action == "reset-params") {
                spawnWidget(w, parent, UIButton{label, [&w] {
                                                    for (auto [entity, sb] : w.reg.view<SoftBodyData>().each()) {
                                                        sb.params = SimParams{};
                                                        sb.steps = kDefaultSteps;
                                                    }
                                                }});
            } else if (action == "demo-window") {
                spawnWidget(w, parent, UIButton{label, [&w] { w.showDemo = true; }});
            } else {
                // behavior: the scene script must register a handler under this name
                spawnWidget(w, parent, UIButton{label, [&w, action] { w.script.button(action); }});
                scriptButtons.push_back(action);
            }
        } else {
            fail(path, "unknown widget type '" + type + "'");
        }
    }
}

void buildUi(World& w, const json& j, const std::string& path) {
    const json& ui = j.value("ui", json::object());
    const auto title = ui.value("title", j.value("name", std::string("scene")));
    const V2 size = ui.contains("size") ? v2(ui["size"], (path + " ui.size").c_str()) : V2{320.0f, 720.0f};
    entt::entity win = spawnUiWindow(w, UIWindow{title, [] { return V2{20.0f, 20.0f}; }, size, 0, true});
    std::vector<std::string> scriptButtons;
    if (ui.contains("widgets")) {
        entt::entity tab = spawnWidget(w, win, UITab{"scene"});
        spawnJsonWidgets(w, tab, ui["widgets"], path, scriptButtons);
    }
    for (const auto& a : scriptButtons)
        if (!w.script.hasButton(a))
            fail(path, "button action '" + a + "': no handler in scene script");
    entt::entity tab = spawnWidget(w, win, UITab{"editor"});
    spawnEditorUi(w, tab);
}

} // namespace

void createSceneWorld(World& w, const std::string& path) {
    const std::string p = resolveFile(path);
    std::ifstream in(p);
    if (!in)
        fail(p, "cannot open file");
    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        fail(p, e.what());
    }

    const json& c = j.at("camera");
    const entt::entity cam =
        spawnCamera(w, v3(c.at("pos"), (p + " camera.pos").c_str()), v4(c.at("quat"), (p + " camera.quat").c_str()),
                    c.value("fov", 50.0f), c.value("active", true));
    if (c.contains("name"))
        w.reg.emplace<Name>(cam, c["name"].get<std::string>());
    w.render.envIntensity = j.value("envIntensity", 1.0f);

    const VkExtent2D ext = w.app->extent();
    w.rigid.init();
    w.sim.init(w.app->device(), w.app->pdev());
    w.renderer.init(*w.app, (int)j["objects"].size() + 2, cameraViewProj(w, cam, (float)ext.width / (float)ext.height));

    // geometry stdlib: the engine always loads "scenes/mesh" as the global `mesh` and the
    // geometry module (for declarative meshes + VR controller grips); scene scripts use the
    // global `mesh` to generate their own geometry
    w.script.init(w);
    std::string err;
    if (!w.script.loadMeshStdlib(err))
        fail(p, "geometry stdlib: " + err);

    for (const auto& L : j["lights"]) {
        const auto name = L.at("name").get<std::string>();
        PointLight pl;
        if (L.contains("color"))
            pl.color = v3(L["color"], (p + " light color").c_str());
        pl.intensity = L.value("intensity", 25.0f);
        pl.radius = L.value("radius", 0.25f);
        pl.on = L.value("on", 1.0f);
        pl.shadowNear = L.value("shadowNear", 0.01f);
        pl.shadowFar = L.value("shadowFar", 30.0f);
        pl.shadowNormalBias = L.value("shadowNormalBias", 0.05f);
        pl.shadowBiasBase = L.value("shadowBiasBase", 0.3f);
        pl.shadowBiasSlope = L.value("shadowBiasSlope", 0.3f);
        pl.shadowSearchScale = L.value("shadowSearchScale", 2.0f);
        pl.shadowMaxRadius = L.value("shadowMaxRadius", 16.0f);
        spawnLight(w, v3(L.at("pos"), (p + " light pos").c_str()), pl, name.c_str());
    }

    // one shared material entity per def; objects reference it (editing one updates all users)
    std::map<std::string, entt::entity> mats;
    for (const auto& m : j.value("materials", json::array())) {
        const auto name = m.at("name").get<std::string>();
        std::string mp = p + " material " + name;
        V3 color{1, 1, 1};
        if (m.contains("baseColor"))
            color = v3(m["baseColor"], mp.c_str());
        const entt::entity me =
            spawnMaterial(w, color, m.value("metallic", 0.0f), m.value("roughness", 0.5f), name.c_str());
        if (m.contains("texture")) {
            const int texId = loadAssetTexture(w, m["texture"].get<std::string>());
            if (texId < 0)
                fail(p, "material '" + name + "': cannot load texture '" + m["texture"].get<std::string>() + "'");
            w.reg.get<Material>(me).texId = texId;
        }
        mats[name] = me;
    }

    for (const auto& o : j["objects"]) {
        const auto name = o.at("name").get<std::string>();
        if (o.contains("script")) {
            // a script entity: no mesh, attaches a per-entity behavior script. "script" is a
            // module path (same convention as the attach_script verb)
            const entt::entity e = w.reg.create();
            auto& ls = w.reg.emplace<LuaScript>(e);
            ls.file = o["script"].get<std::string>();
            w.reg.emplace<Name>(e, name);
            w.reg.emplace<Transform>(e); // identity; lets the entity parent children
            continue;
        }
        const json& mj = o.at("mesh");
        const auto type = mj.at("type").get<std::string>();
        // mesh params (everything but "type") go to the std generator as a table
        std::vector<std::pair<std::string, double>> params;
        for (const auto& [k, v] : mj.items()) {
            if (k == "type")
                continue;
            if (!v.is_number())
                fail(p, "object '" + name + "': mesh param '" + k + "' must be a number");
            params.emplace_back(k, v.get<double>());
        }
        const int geom = w.script.callMeshGen(type, params, err);
        if (geom < 0)
            fail(p, "object '" + name + "': mesh '" + type + "': " + err);

        Transform t{};
        if (o.contains("pos"))
            t.pos = v3(o["pos"], (p + " object " + name + " pos").c_str());
        if (o.contains("quat"))
            t.quat = v4(o["quat"], (p + " object " + name + " quat").c_str());

        const entt::entity e = spawnStaticGeom(w, geom, t);
        w.reg.emplace<Name>(e, name);

        const auto matName = o.at("material").get<std::string>();
        auto it = mats.find(matName);
        if (it == mats.end())
            fail(p, "object '" + name + "': unknown material '" + matName + "'");
        w.reg.emplace<MaterialRef>(e, it->second);
    }

    // activate every script entity to fixpoint; a _on_ready may spawn more script entities,
    // which activate in the same pass. Script entities are declared per-object in the JSON
    // ("script" field) and run before sim.build() and the UI so URLs and the editor tab see
    // their spawned entities
    w.script.activateScripts();

    // set block after the script: URLs may point at script-spawned entities
    if (j.contains("set")) {
        for (const auto& [k, v] : j["set"].items()) {
            float vals[3];
            int n = v.is_array() ? (int)v.size() : 1;
            if (n > 3)
                fail(p, "set '" + k + "': too many values");
            if (v.is_array())
                for (int i = 0; i < n; ++i)
                    vals[i] = v[(size_t)i].get<float>();
            else
                vals[0] = v.get<float>();
            std::string err;
            if (!sceneSet(w, k, vals, n, err))
                fail(p, "set '" + k + "': " + err);
        }
    }

    w.sim.build();
    if (j.contains("env")) {
        const auto env = j["env"].get<std::string>();
        HdrData hdr;
        if (!loadHdr(resolveFile("assets/" + env), hdr))
            fail(p, "cannot load env '" + env + "'");
        w.renderer.setEnvironment(*w.app, hdr.rgb.data(), (uint32_t)hdr.w, (uint32_t)hdr.h);
    }
    buildUi(w, j, p);
}
