#include "url.hpp"

#include "world.hpp"
#include <cstring>
#include <entt/entt.hpp>

static const FieldDesc* findField(const TypeDesc* t, const std::string& name) {
    for (const auto& f : t->fields)
        if (name == f.name)
            return &f;
    return nullptr;
}

// resolve a field on a specific entity (no name lookup): optional Component qualifier
// ("Component.field"), then a MaterialRef fallthrough. label is for error messages.
bool resolveEntityField(const World& w, entt::entity ent, const std::string& field, const std::string& label,
                        UrlRef& out, std::string& err) {
    auto comps = refl::entityFields(w.reg, ent);
    if (comps.empty()) {
        err = "entity '" + label + "' has no reflectable components";
        return false;
    }

    // optional qualifier: Component.field
    const TypeDesc* qual = nullptr;
    if (const auto d2 = field.find('.'); d2 != std::string::npos)
        for (auto& [t, b] : comps)
            if (field.substr(0, d2) == t->name)
                qual = t;

    const FieldDesc* match = nullptr;
    void* matchBase = nullptr;
    int matches = 0;
    for (auto& [t, b] : comps) {
        if (qual && t != qual)
            continue;
        if (const FieldDesc* f = findField(t, field)) {
            match = f;
            matchBase = (char*)b + f->offset;
            ++matches;
        }
    }
    // field not on the entity: follow its MaterialRef to the shared material (indirection), so
    // "ball.roughness" addresses the material many meshes may share
    if (matches == 0)
        if (const auto* ref = w.reg.try_get<MaterialRef>(ent))
            for (auto& [t, b] : refl::entityFields(w.reg, ref->mat)) {
                if (const FieldDesc* f = findField(t, field)) {
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

bool resolveUrl(const World& w, const std::string& url, UrlRef& out, std::string& err) {
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

// write vals into a resolved field; false on a kind/count mismatch
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
    case FieldKind::Ref:
        err = "field is read-only";
        return false;
    }
    return false;
}

// read a resolved field into vals (maxN floats); out n = 1 for scalars, 3 for Color/DragV3
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
    case FieldKind::Ref:
        err = "field is an entity reference";
        return false;
    }
    return false;
}

bool sceneSet(World& w, const std::string& url, const float* vals, int n, std::string& err) {
    UrlRef r;
    if (!resolveUrl(w, url, r, err))
        return false;
    return applySet(r, vals, n, err);
}

bool sceneGet(const World& w, const std::string& url, float* vals, int maxN, int& n, std::string& err) {
    UrlRef r;
    if (!resolveUrl(w, url, r, err))
        return false;
    return applyGet(r, vals, maxN, n, err);
}

// handle form: address a field on a specific entity (arg-1 = handle, not a name URL)
bool sceneSetEnt(World& w, entt::entity ent, const std::string& field, const float* vals, int n, std::string& err) {
    UrlRef r;
    if (!resolveEntityField(w, ent, field, "entity", r, err))
        return false;
    return applySet(r, vals, n, err);
}

bool sceneGetEnt(const World& w, entt::entity ent, const std::string& field, float* vals, int maxN, int& n,
                 std::string& err) {
    UrlRef r;
    if (!resolveEntityField(w, ent, field, "entity", r, err))
        return false;
    return applyGet(r, vals, maxN, n, err);
}
