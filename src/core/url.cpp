#include "url.hpp"

#include <cstring>
#include <entt/entt.hpp>

static const FieldDesc* findField(const TypeDesc* t, const std::string& name) {
    for (const auto& f : t->fields)
        if (name == f.name)
            return &f;
    return nullptr;
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

bool resolveUrl(const entt::registry& reg, const std::vector<refl::RootRef>& roots, const std::string& url, UrlRef& out,
                std::string& err) {
    const auto dot = url.find('.');
    if (dot == std::string::npos) {
        err = "expected 'name.Component.field'";
        return false;
    }
    const std::string head = url.substr(0, dot);

    if (head == "render" || head == "rigid") {
        const std::string rest = url.substr(dot + 1);
        const char* wantName = head == "render" ? "RenderSettings" : "RigidScene";
        for (const auto& root : roots) {
            if (std::strcmp(root.type->name, wantName) != 0)
                continue;
            const FieldDesc* f = findField(root.type, rest);
            if (!f) {
                err = "no field '" + rest + "' in " + head;
                return false;
            }
            out = {f, (char*)root.base + f->offset};
            return true;
        }
        err = "no '" + head + "' root";
        return false;
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

bool sceneSet(const entt::registry& reg, const std::vector<refl::RootRef>& roots, const std::string& url, const float* vals,
              int n, std::string& err) {
    UrlRef r;
    if (!resolveUrl(reg, roots, url, r, err))
        return false;
    return applySet(r, vals, n, err);
}

bool sceneGet(const entt::registry& reg, const std::vector<refl::RootRef>& roots, const std::string& url, float* vals,
              int maxN, int& n, std::string& err) {
    UrlRef r;
    if (!resolveUrl(reg, roots, url, r, err))
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
