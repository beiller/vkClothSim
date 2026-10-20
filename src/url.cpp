#include "url.hpp"

#include "engine.hpp"
#include <cstring>
#include <entt/entt.hpp>

static const FieldDesc* findField(const TypeDesc* t, const std::string& name) {
    for (const auto& f : t->fields)
        if (name == f.name)
            return &f;
    return nullptr;
}

bool resolveEntityField(const Engine& w, entt::entity ent, const std::string& field, const std::string& label,
                        UrlRef& out, std::string& err) {
    auto comps = refl::entityFields(w.reg, ent);
    if (comps.empty()) {
        err = "entity '" + label + "' has no reflectable components";
        return false;
    }

    const TypeDesc* qual = nullptr;
    size_t dot = field.find('.');
    if (dot != std::string::npos)
        for (auto& [t, b] : comps)
            if (field.substr(0, dot) == t->name)
                qual = t;
    const std::string fn = qual ? field.substr(dot + 1) : field;

    const FieldDesc* match = nullptr;
    void* matchBase = nullptr;
    int matches = 0;
    for (auto& [t, b] : comps) {
        if (qual && t != qual)
            continue;
        if (const FieldDesc* f = findField(t, fn)) {
            match = f;
            matchBase = (char*)b + f->offset;
            ++matches;
        }
    }
    if (matches == 0 && !qual)
        if (const auto* ref = w.reg.try_get<MaterialRef>(ent))
            for (auto& [t, b] : refl::entityFields(w.reg, ref->mat)) {
                if (const FieldDesc* f = findField(t, fn)) {
                    match = f;
                    matchBase = (char*)b + f->offset;
                    ++matches;
                }
            }
    if (matches == 0) {
        err = "no field '" + field + "' on '" + label + "'";
        return false;
    }
    if (matches > 1) {
        err = "field '" + field + "' on '" + label + "' is ambiguous; use <Component>." + field;
        return false;
    }
    out = {match, matchBase};
    return true;
}

bool resolveUrl(const Engine& w, const std::string& url, UrlRef& out, std::string& err) {
    const auto dot = url.find('.');
    if (dot == std::string::npos) {
        err = "expected 'name.field'";
        return false;
    }
    const std::string head = url.substr(0, dot);
    const std::string rest = url.substr(dot + 1);

    if (head == "render" || head == "rigid") {
        const auto* root = refl::findType(head == "render" ? "RenderSettings" : "RigidScene");
        void* base = head == "render" ? (void*)&w.render : (void*)&w.rigid;
        const FieldDesc* f = findField(root, rest);
        if (!f) {
            err = "no field '" + rest + "' in " + head;
            return false;
        }
        out = {f, (char*)base + f->offset};
        return true;
    }

    entt::entity ent = entt::null;
    int found = 0;
    for (auto [e, n] : w.reg.view<Name>().each()) {
        if (n.id == head) {
            ent = e;
            ++found;
        }
        if (found > 1) {
            err = "entity name '" + head + "' is not unique";
            return false;
        }
    }
    if (found == 0) {
        err = "unknown entity '" + head + "'";
        return false;
    }

    return resolveEntityField(w, ent, rest, head, out, err);
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

bool sceneSet(Engine& w, const std::string& url, const float* vals, int n, std::string& err) {
    UrlRef r;
    if (!resolveUrl(w, url, r, err))
        return false;
    return applySet(r, vals, n, err);
}

bool sceneGet(const Engine& w, const std::string& url, float* vals, int maxN, int& n, std::string& err) {
    UrlRef r;
    if (!resolveUrl(w, url, r, err))
        return false;
    return applyGet(r, vals, maxN, n, err);
}

bool sceneSetEnt(Engine& w, entt::entity ent, const std::string& field, const float* vals, int n, std::string& err) {
    UrlRef r;
    if (!resolveEntityField(w, ent, field, "entity", r, err))
        return false;
    return applySet(r, vals, n, err);
}

bool sceneGetEnt(const Engine& w, entt::entity ent, const std::string& field, float* vals, int maxN, int& n,
                 std::string& err) {
    UrlRef r;
    if (!resolveEntityField(w, ent, field, "entity", r, err))
        return false;
    return applyGet(r, vals, maxN, n, err);
}
