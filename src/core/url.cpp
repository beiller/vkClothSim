#include "url.hpp"

#include <cstring>
#include <entt/entt.hpp>

// "LuaScript.var" -> a slot in the script's vars mirror. The var path may be nested
// ("pos.x"); subvars of 3/4-number table slots resolve to member views into the
// contiguous vec storage. Numbers are float, plus bool/vec3/quat; string vars are not
// bindable (read/write them with scene.get/scene.set).
template <typename M>
static M& varMap(const M& m) {
    // the registry view is const-only in interface; the storage is mutable
    return const_cast<M&>(m);
}

static bool resolveVar(const entt::registry& reg, entt::entity ent, const std::string& path, const std::string& label,
                       UrlRef& out, std::string& err) {
    const auto* ls = reg.try_get<LuaScript>(ent);
    if (!ls || ls->inst < 0) {
        err = "entity '" + label + "' has no script";
        return false;
    }
    static const FieldDesc fF{"var", 0, FieldKind::SliderF};
    static const FieldDesc fB{"var", 0, FieldKind::Checkbox};
    static const FieldDesc fV3{"var", 0, FieldKind::DragV3};
    static const FieldDesc fQ{"var", 0, FieldKind::Quat};

    // "head.tail" where head is a vec slot: view into its members (no separate storage)
    const size_t dot = path.rfind('.');
    if (dot != std::string::npos) {
        const std::string head = path.substr(0, dot);
        const std::string tail = path.substr(dot + 1);
        if (auto it = varMap(ls->varsV3).find(head); it != varMap(ls->varsV3).end()) {
            float* m = nullptr;
            if (tail == "x")
                m = &it->second.x;
            else if (tail == "y")
                m = &it->second.y;
            else if (tail == "z")
                m = &it->second.z;
            if (m) {
                out = {&fF, m};
                return true;
            }
        }
        if (auto it = varMap(ls->varsQ).find(head); it != varMap(ls->varsQ).end()) {
            float* m = nullptr;
            if (tail == "x")
                m = &it->second.x;
            else if (tail == "y")
                m = &it->second.y;
            else if (tail == "z")
                m = &it->second.z;
            else if (tail == "w")
                m = &it->second.w;
            if (m) {
                out = {&fF, m};
                return true;
            }
        }
    }
    if (auto it = varMap(ls->varsV3).find(path); it != varMap(ls->varsV3).end()) {
        out = {&fV3, &it->second};
        return true;
    }
    if (auto it = varMap(ls->varsQ).find(path); it != varMap(ls->varsQ).end()) {
        out = {&fQ, &it->second};
        return true;
    }
    if (auto it = varMap(ls->varsF).find(path); it != varMap(ls->varsF).end()) {
        out = {&fF, &it->second};
        return true;
    }
    if (auto it = varMap(ls->varsB).find(path); it != varMap(ls->varsB).end()) {
        out = {&fB, &it->second};
        return true;
    }
    err = "no var '" + path + "' in script of '" + label + "'";
    return false;
}

bool resolveEntityField(const entt::registry& reg, entt::entity ent, const std::string& field, const std::string& label,
                        UrlRef& out, std::string& err) {
    const size_t dot = field.rfind('.');
    if (dot == std::string::npos) {
        err = "field must be 'Component.field'";
        return false;
    }
    const std::string cn = field.substr(0, dot);
    const std::string fn = field.substr(dot + 1);
    if (cn == "LuaScript")
        return resolveVar(reg, ent, fn, label, out, err);
    for (auto& [t, b] : refl::entityFields(reg, ent)) {
        if (t->name != cn)
            continue;
        const FieldDesc* f = findField(t, fn);
        if (!f) {
            err = "no field '" + fn + "' in " + cn;
            return false;
        }
        const void* bp = b();
        if (!bp) {
            err = "entity '" + label + "' has no " + cn;
            return false;
        }
        out = {f, (char*)bp + f->offset};
        return true;
    }
    err = "no component '" + cn + "' on '" + label + "'";
    return false;
}

bool resolveUrl(const entt::registry& reg, const std::string& url, UrlRef& out, std::string& err) {
    const auto dot = url.find('.');
    if (dot == std::string::npos) {
        err = "expected 'name.Component.field'";
        return false;
    }
    const std::string head = url.substr(0, dot);

    if (head == "render" || head == "rigid") {
        entt::entity settings = entt::null;
        for (auto e : reg.view<SettingsRoot>()) {
            settings = e;
            break;
        }
        if (settings == entt::null) {
            err = "no 'settings' entity";
            return false;
        }
        const std::string cf = (head == "render" ? "RenderSettings." : "PhysicsSettings.") + url.substr(dot + 1);
        return resolveEntityField(reg, settings, cf, head, out, err);
    }

    // "name.LuaScript.varpath": the marker anchors the split even when the entity name
    // contains dots; on a miss, fall through to the generic split (an entity may be
    // literally named "X.LuaScript.Y").
    const size_t marker = url.rfind(".LuaScript.");
    if (marker != std::string::npos && marker > 0 && marker + 11 < url.size()) {
        const std::string mname = url.substr(0, marker);
        entt::entity ment = entt::null;
        int mfound = 0;
        for (auto [e, n] : reg.view<Name>().each())
            if (n.id == mname) {
                ment = e;
                ++mfound;
            }
        if (mfound > 1) {
            err = "entity name '" + mname + "' is not unique";
            return false;
        }
        if (mfound == 1) {
            std::string verr;
            if (resolveVar(reg, ment, url.substr(marker + 11), mname, out, verr))
                return true;
        }
    }

    // entity names may contain dots: the last two dot segments are Component.field
    const auto lastDot = url.rfind('.');
    const auto splitDot = url.rfind('.', lastDot - 1);
    if (lastDot == std::string::npos || splitDot == std::string::npos) {
        err = "expected 'name.Component.field'";
        return false;
    }
    const std::string name = url.substr(0, splitDot);
    const std::string rest = url.substr(splitDot + 1);

    entt::entity ent = entt::null;
    int found = 0;
    for (auto [e, n] : reg.view<Name>().each()) {
        if (n.id == name) {
            ent = e;
            ++found;
        }
        if (found > 1) {
            err = "entity name '" + name + "' is not unique";
            return false;
        }
    }
    if (found == 0) {
        err = "unknown entity '" + name + "'";
        return false;
    }

    return resolveEntityField(reg, ent, rest, name, out, err);
}

static bool applySet(const UrlRef& r, const float* vals, int n, std::string& err) {
    switch (r.field->kind) {
    case FieldKind::SliderF:
    case FieldKind::Color:
    case FieldKind::DragV3: {
        const int want = r.field->kind == FieldKind::SliderF ? 1 : 3;
        if (n != want) {
            err = "field expects " + std::to_string(want) + " value(s)";
            return false;
        }
        std::memcpy(r.base, vals, (size_t)want * sizeof(float));
        return true;
    }
    case FieldKind::SliderI:
        if (n != 1) {
            err = "field expects 1 value";
            return false;
        }
        *(int*)r.base = (int)vals[0];
        return true;
    case FieldKind::Checkbox:
        if (n != 1) {
            err = "field expects 1 value";
            return false;
        }
        *(bool*)r.base = vals[0] != 0.0f;
        return true;
    case FieldKind::Quat:
        if (n != 4) {
            err = "field expects 4 values";
            return false;
        }
        std::memcpy(r.base, vals, 16);
        return true;
    case FieldKind::Ref:
        err = "field is an entity reference (set it with scene.replace)";
        return false;
    case FieldKind::Str:
        err = "field is a string (set it with scene.replace)";
        return false;
    case FieldKind::FloatArr:
    case FieldKind::UIntArr:
    case FieldKind::EntArr:
        err = "field is an array (set it with scene.replace)";
        return false;
    }
    return false;
}

static bool applyGet(const UrlRef& r, float* vals, int maxN, int& n, std::string& err) {
    switch (r.field->kind) {
    case FieldKind::SliderF:
    case FieldKind::Color:
    case FieldKind::DragV3: {
        const int want = r.field->kind == FieldKind::SliderF ? 1 : 3;
        if (maxN < want) {
            err = "field expects " + std::to_string(want) + " value(s)";
            return false;
        }
        std::memcpy(vals, r.base, (size_t)want * sizeof(float));
        n = want;
        return true;
    }
    case FieldKind::SliderI:
        if (maxN < 1)
            return false;
        vals[0] = (float)*(int*)r.base;
        n = 1;
        return true;
    case FieldKind::Checkbox:
        if (maxN < 1)
            return false;
        vals[0] = *(bool*)r.base ? 1.0f : 0.0f;
        n = 1;
        return true;
    case FieldKind::Quat:
        if (maxN < 4) {
            err = "field expects 4 values";
            return false;
        }
        std::memcpy(vals, r.base, 16);
        n = 4;
        return true;
    case FieldKind::Ref:
        err = "field is an entity reference (read it with scene.component)";
        return false;
    case FieldKind::Str:
    case FieldKind::FloatArr:
    case FieldKind::UIntArr:
    case FieldKind::EntArr:
        err = "field is not a number (read it with scene.component)";
        return false;
    }
    return false;
}

bool sceneSet(const entt::registry& reg, const std::string& url, const float* vals, int n, std::string& err) {
    UrlRef r;
    if (!resolveUrl(reg, url, r, err))
        return false;
    return applySet(r, vals, n, err);
}

bool sceneGet(const entt::registry& reg, const std::string& url, float* vals, int maxN, int& n, std::string& err) {
    UrlRef r;
    if (!resolveUrl(reg, url, r, err))
        return false;
    return applyGet(r, vals, maxN, n, err);
}

bool sceneSetEnt(const entt::registry& reg, entt::entity ent, const std::string& field, const float* vals, int n,
                 std::string& err) {
    UrlRef r;
    if (!resolveEntityField(reg, ent, field, "entity", r, err))
        return false;
    return applySet(r, vals, n, err);
}

bool sceneGetEnt(const entt::registry& reg, entt::entity ent, const std::string& field, float* vals, int maxN, int& n,
                 std::string& err) {
    UrlRef r;
    if (!resolveEntityField(reg, ent, field, "entity", r, err))
        return false;
    return applyGet(r, vals, maxN, n, err);
}
