#include "scene.hpp"

#include "assets.hpp"
#include "components.hpp"
#include "gltf.hpp"
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
#include <stdexcept>
#include <vector>

namespace {

using nlohmann::json;

[[noreturn]] void fail(const std::string& path, const std::string& msg) {
    throw SceneError("scene " + path + ": " + msg);
}

std::string resolveFile(const std::string& p) {
    return std::filesystem::exists(p) ? p : exeDir() + "/../" + p;
}

void addComponents(entt::registry& reg, entt::entity e, const json& cj, const std::string& p, const std::string& what) {
    for (const auto& [tn, fj] : cj.items()) {
        CompFields fields;
        for (const auto& [fn, fv] : fj.items()) {
            CompField f;
            f.name = fn;
            if (fv.is_number() || fv.is_boolean()) {
                f.v[0] = fv.is_boolean() ? (fv.get<bool>() ? 1.0f : 0.0f) : fv.get<float>();
                f.n = 1;
                if (fv.is_number() && fv.is_number_integer()) {
                    f.hasEnt = true;
                    f.ent = (entt::entity)fv.get<int64_t>();
                }
            } else if (fv.is_array()) {
                f.isArr = true;
                if (fv.size() <= 4)
                    for (size_t i = 0; i < fv.size(); ++i) {
                        if (!fv[i].is_number())
                            fail(p, what + " field '" + fn + "': values must be numbers");
                        f.v[i] = fv[i].get<float>();
                    }
                else
                    for (size_t i = 0; i < fv.size(); ++i) {
                        if (!fv[i].is_number())
                            fail(p, what + " field '" + fn + "': values must be numbers");
                        f.arr.push_back(fv[i].get<float>());
                    }
                f.n = (int)fv.size();
            } else if (fv.is_string()) {
                f.isStr = true;
                f.str = fv.get<std::string>();
            } else {
                fail(p, what + " field '" + fn + "': expected a number, boolean, string, or array");
            }
            fields.push_back(f);
        }
        std::string err;
        if (!compEmplace(reg, e, tn, fields, err))
            fail(p, what + " component '" + tn + "': " + err);
    }
}

entt::entity buildObject(Engine& w, const json& o, const std::string& p, bool allowMesh) {
    const auto name = o.at("name").get<std::string>();
    const std::string what = "object '" + name + "'";
    const entt::entity e = w.reg.create();
    w.reg.emplace<Name>(e, name);

    if (o.contains("components"))
        addComponents(w.reg, e, o["components"], p, what + ":");

    if (o.contains("gltf")) {
        if (!allowMesh)
            fail(p, what + ": the camera object cannot have a gltf");
        const auto gpath = o["gltf"].get<std::string>();
        std::string gerr;
        if (loadGltf(w.reg, e, gpath, gerr) == entt::null)
            fail(p, what + ": gltf '" + gpath + "': " + gerr);
    }

    if (o.contains("script")) {
        auto& ls = w.reg.emplace<LuaScript>(e);
        ls.file = o["script"].get<std::string>();
    } else if (!o.contains("gltf") && !w.reg.try_get<Transform>(e)) {
        w.reg.emplace<Transform>(e);
    }
    return e;
}

}

void spawnEditorUi(Engine& w, entt::entity tab) {
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

void initEngine(Engine& w) {
    w.rigid.init();
    w.gpuSim.init(w.app->device(), w.app->pdev());
    w.renderer.init(*w.app, 256, mat4Identity());
    w.script.init(w);
    std::string err;
    if (!w.script.loadMeshStdlib(err)) {
        std::fprintf(stderr, "3dsim: geometry stdlib: %s\n", err.c_str());
        std::exit(1);
    }
    w.script.watch(w.reg);
    w.renderer.watch(w.reg);
    w.rigid.watch(w.reg);
    w.gpuSim.soft.watch(w.reg);
    w.gpuSim.skin.watch(w.reg);
}

void resetScene(Engine& w) {
    w.renderer.flushFrees();
    w.gpuSim.soft.flushFrees();
    w.gpuSim.skin.flushFrees();
    w.reg.clear();
    w.rigid.reset();
    w.renderer.reset();
    w.gpuSim.soft.reset();
    w.gpuSim.skin.reset();
    w.script.shutdown();
    w.script.init(w);
    std::string err;
    if (!w.script.loadMeshStdlib(err))
        std::fprintf(stderr, "3dsim: geometry stdlib: %s\n", err.c_str());
    w.uiSeq = 0;
    w.showDemo = false;
    w.render = {};
    spawnFpsHud(w);
    spawnInputEntities(w);
    if (w.xr && w.xrSession) {
        spawnVrRoot(w.reg);
        spawnControllers(w);
    }
}

bool createScene(Engine& w, const std::string& path, std::string& err) {
    const std::string p = resolveFile(path);
    std::ifstream in(p);
    if (!in) {
        err = "cannot open file";
        return false;
    }
    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }

    try {
        w.render.envIntensity = j.value("envIntensity", 1.0f);
        const json& objects = j.value("objects", json::array());

        const json* camObj = nullptr;
        for (const auto& o : objects)
            if (o.contains("components") && o["components"].contains("Camera")) {
                if (camObj)
                    fail(p, "scene must declare exactly one camera object (an object with a 'Camera' component)");
                camObj = &o;
            }
        if (!camObj)
            fail(p, "scene must declare exactly one camera object (an object with a 'Camera' component)");
        const entt::entity cam = buildObject(w, *camObj, p, false);
        w.reg.emplace<SceneCamera>(cam);

        const VkExtent2D ext = w.app->extent();
        w.renderer.setViewProj(cameraView(w.reg, cam, (float)ext.width / (float)ext.height));

        for (const auto& o : objects)
            if (&o != camObj)
                buildObject(w, o, p, true);

        w.script.activateScripts();

        if (j.contains("set")) {
            for (const auto& [k, v] : j["set"].items()) {
                float vals[4];
                int n = v.is_array() ? (int)v.size() : 1;
                if (n > 4)
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

        w.gpuSim.build(w.reg);
        if (j.contains("env")) {
            const auto env = j["env"].get<std::string>();
            HdrData hdr;
            if (!loadHdr(resolveFile("assets/" + env), hdr))
                fail(p, "cannot load env '" + env + "'");
            w.renderer.setEnvironment(*w.app, hdr.rgb.data(), (uint32_t)hdr.w, (uint32_t)hdr.h);
        }
        w.script.buildUi(j);
        return true;
    } catch (const SceneError& e) {
        err = e.what();
        return false;
    }
}

bool loadScene(Engine& w, const std::string& path) {
    resetScene(w);
    std::string err;
    if (!createScene(w, path, err)) {
        w.loadError = err;
        return false;
    }
    w.loadError = "";
    w.scenePath = path;
    std::printf("3dsim: scene %s\n", path.c_str());
    return true;
}
