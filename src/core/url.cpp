#include "url.hpp"

#include <cstring>
#include <entt/entt.hpp>
#include <unordered_map>

// Per-URL resolution cache. Entries store only what is needed to re-derive the field
// address (comp + entity + FieldDesc); the base pointer itself is never stored, so a hit
// is valid iff the component still exists on the entity (checked via tryGet). .LuaScript.
// URLs never reach the store: they only succeed through resolveVar, which returns
// map-backed pointers that are not stable across syncs.
struct UrlEntry {
    const CompReg* comp = nullptr;
    entt::entity ent = entt::null;
    const FieldDesc* field = nullptr;
};
static std::unordered_map<std::string, UrlEntry> s_urlCache;

void urlInvalidateAll() {
    s_urlCache.clear();
}

template <typename M> static M& varMap(const M& m) {
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
            err = "no field '";
            err += fn;
            err += "' in ";
            err += cn;
            return false;
        }
        const void* bp = b();
        if (!bp) {
            err = "entity '";
            err += label;
            err += "' has no ";
            err += cn;
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

    if (const auto it = s_urlCache.find(url); it != s_urlCache.end()) { // fast path: liveness-checked hit
        const UrlEntry& e = it->second;
        if (e.comp && e.comp->tryGet(reg, e.ent)) {
            out = {e.field, (char*)e.comp->tryGet(reg, e.ent) + e.field->offset};
            return true;
        }
    }

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

    UrlRef r;
    if (!resolveEntityField(reg, ent, rest, name, r, err))
        return false;
    out = r;
    if (url.find(".LuaScript.") == std::string::npos) { // map-backed values have no stable pointer: never cached
        s_urlCache[url] = {compFor(rest.substr(0, rest.find('.'))), ent,
                           r.field}; // comp may be null: degrades to no cache
    }
    return true;
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
    default:
        break;
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
    default:
        break;
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
