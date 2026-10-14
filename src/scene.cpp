#include "scene.hpp"

#include "assets.hpp"
#include "components.hpp"
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

V2 v2(const json& a, const char* what) {
    if (!a.is_array() || a.size() != 2)
        fail(what, "expected [x, y]");
    return {a[0].get<float>(), a[1].get<float>()};
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

// ---- scene objects

// a "components" block: emplace each named component (struct-shape fields, the same
// vocabulary as Lua scene.emplace; omitted fields keep the C++ struct defaults)
void addComponents(entt::registry& reg, entt::entity e, const json& cj, const std::string& p, const std::string& what) {
    for (const auto& [tn, fj] : cj.items()) {
        CompFields fields;
        for (const auto& [fn, fv] : fj.items()) {
            CompField f;
            f.name = fn;
            if (fv.is_number() || fv.is_boolean()) {
                f.v[0] = fv.is_boolean() ? (fv.get<bool>() ? 1.0f : 0.0f) : fv.get<float>();
                f.n = 1;
            } else if (fv.is_array()) {
                if (fv.size() > 4)
                    fail(p, what + " field '" + fn + "': expected at most 4 values");
                for (size_t i = 0; i < fv.size(); ++i) {
                    if (!fv[i].is_number())
                        fail(p, what + " field '" + fn + "': values must be numbers");
                    f.v[i] = fv[i].get<float>();
                }
                f.n = (int)fv.size();
            } else {
                fail(p, what + " field '" + fn + "': expected a number, boolean, or array");
            }
            fields.push_back(f);
        }
        std::string err;
        if (!compEmplace(reg, e, tn, fields, err))
            fail(p, what + " component '" + tn + "': " + err);
    }
}

// one scene object: Name + "components" + "texture" (a path on a texture object = shared texture
// entity; a name on a material object = reference to an earlier texture object) + "mesh" (render.mesh
// equivalent; placed by the components' Transform) + "script" (behavior).
// Referenced names must be defined earlier in "objects" (define before use)
void buildObject(World& w, const json& o, const std::string& p, bool allowMesh) {
    const auto name = o.at("name").get<std::string>();
    const std::string what = "object '" + name + "'";
    const entt::entity e = w.reg.create();
    w.reg.emplace<Name>(e, name);

    if (o.contains("components"))
        addComponents(w.reg, e, o["components"], p, what + ":");

    if (o.contains("texture")) {
        const auto tex = o["texture"].get<std::string>();
        if (w.reg.try_get<Material>(e)) {
            // a material references an earlier texture object by name (define before use)
            const entt::entity texE = findNamedEntity(w, tex);
            if (texE == entt::null || !w.reg.try_get<Texture>(texE))
                fail(p, what + ": unknown texture '" + tex + "' (define the texture object first)");
            w.reg.get<Material>(e).tex = texE;
        } else if (!textureFor(w, e, tex)) { // texture object: a top-level entity from an asset path
            fail(p, what + ": cannot load texture '" + tex + "'");
        }
    }

    if (o.contains("mesh")) {
        if (!allowMesh)
            fail(p, what + ": the camera object cannot have a mesh");
        const json& mj = o.at("mesh");
        const auto type = mj.at("type").get<std::string>();
        // mesh params (everything but "type") go to the std generator as a table
        std::vector<std::pair<std::string, double>> params;
        for (const auto& [k, v] : mj.items()) {
            if (k == "type")
                continue;
            if (!v.is_number())
                fail(p, what + ": mesh param '" + k + "' must be a number");
            params.emplace_back(k, v.get<double>());
        }
        std::string err;
        const int geom = w.script.callMeshGen(type, params, err);
        if (geom < 0)
            fail(p, what + ": mesh '" + type + "': " + err);
        const auto matName = o.at("material").get<std::string>();
        auto matE = findNamedEntity(w, matName);
        if (matE == entt::null || !w.reg.try_get<Material>(matE))
            fail(p, what + ": unknown material '" + matName + "'");
        addMesh(w, e, geom, matE, entt::null);
    }

    if (o.contains("script")) {
        auto& ls = w.reg.emplace<LuaScript>(e);
        ls.file = o["script"].get<std::string>();
    } else if (!o.contains("mesh") && !w.reg.try_get<Transform>(e)) {
        w.reg.emplace<Transform>(e); // identity; lets a plain container parent children
    }
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

    w.render.envIntensity = j.value("envIntensity", 1.0f);
    const json& objects = j.value("objects", json::array());

    // the scene's camera: exactly one object with a Camera component. It is built first so the
    // renderer can start from its pose (VR later spawns its own "vr cam")
    const json* camObj = nullptr;
    for (const auto& o : objects)
        if (o.contains("components") && o["components"].contains("Camera")) {
            if (camObj)
                fail(p, "scene must declare exactly one camera object (an object with a 'Camera' component)");
            camObj = &o;
        }
    if (!camObj)
        fail(p, "scene must declare exactly one camera object (an object with a 'Camera' component)");
    buildObject(w, *camObj, p, false);
    for (auto e : w.reg.view<Camera>())
        w.cam = e;

    const VkExtent2D ext = w.app->extent();
    w.rigid.init();
    w.sim.init(w.app->device(), w.app->pdev());
    w.renderer.init(*w.app, (int)objects.size() + 2, cameraViewProj(w, w.cam, (float)ext.width / (float)ext.height));

    // geometry stdlib: the engine always loads "scenes/mesh" as the global `mesh` and the
    // geometry module (for declarative meshes + VR controller grips); scene scripts use the
    // global `mesh` to generate their own geometry
    w.script.init(w);
    std::string err;
    if (!w.script.loadMeshStdlib(err))
        fail(p, "geometry stdlib: " + err);

    for (const auto& o : objects)
        if (&o != camObj)
            buildObject(w, o, p, true);

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
