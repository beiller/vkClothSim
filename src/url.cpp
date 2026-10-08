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

bool resolveUrl(World& w, const std::string& url, UrlRef& out, std::string& err) {
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

    auto comps = refl::entityFields(w.reg, ent);
    if (comps.empty()) {
        err = "entity '" + head + "' has no reflectable components";
        return false;
    }

    // optional qualifier: head.Component.field
    const TypeDesc* qual = nullptr;
    if (const auto d2 = rest.find('.'); d2 != std::string::npos)
        for (auto& [t, b] : comps)
            if (rest.substr(0, d2) == t->name)
                qual = t;

    const FieldDesc* match = nullptr;
    void* matchBase = nullptr;
    int matches = 0;
    for (auto& [t, b] : comps) {
        if (qual && t != qual)
            continue;
        if (const FieldDesc* f = findField(t, rest)) {
            match = f;
            matchBase = (char*)b + f->offset;
            ++matches;
        }
    }
    if (matches == 0) {
        err = "no field '" + rest + "' on '" + head + "'";
        return false;
    }
    if (matches > 1) {
        err = "field '" + rest + "' on '" + head + "' is ambiguous; use " + head + ".<Component>." + rest;
        return false;
    }
    out = {match, matchBase};
    return true;
}

bool sceneSet(World& w, const std::string& url, const float* vals, int n, std::string& err) {
    UrlRef r;
    if (!resolveUrl(w, url, r, err))
        return false;
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
    }
    return false;
}
