#include "components.hpp"

#include "reflect.hpp"
#include <cstring>

namespace {

enum class Comp { Transform, Material, Camera, PointLight, None };

Comp compFor(const std::string& name) {
    if (name == "Transform")
        return Comp::Transform;
    if (name == "Material")
        return Comp::Material;
    if (name == "Camera")
        return Comp::Camera;
    if (name == "PointLight")
        return Comp::PointLight;
    return Comp::None;
}

const char* compName(Comp c) {
    switch (c) {
    case Comp::Transform:
        return "Transform";
    case Comp::Material:
        return "Material";
    case Comp::Camera:
        return "Camera";
    case Comp::PointLight:
        return "PointLight";
    }
    return nullptr;
}

bool present(const entt::registry& reg, entt::entity e, Comp c) {
    switch (c) {
    case Comp::Transform:
        return reg.try_get<Transform>(e) != nullptr;
    case Comp::Material:
        return reg.try_get<Material>(e) != nullptr;
    case Comp::Camera:
        return reg.try_get<Camera>(e) != nullptr;
    case Comp::PointLight:
        return reg.try_get<PointLight>(e) != nullptr;
    }
    return false;
}

void emplaceDefault(entt::registry& reg, entt::entity e, Comp c) {
    switch (c) {
    case Comp::Transform:
        reg.emplace<Transform>(e);
        break;
    case Comp::Material:
        reg.emplace<Material>(e);
        break;
    case Comp::Camera:
        reg.emplace<Camera>(e);
        break;
    case Comp::PointLight:
        reg.emplace<PointLight>(e);
        break;
    }
}

void* base(entt::registry& reg, entt::entity e, Comp c) {
    switch (c) {
    case Comp::Transform:
        return reg.try_get<Transform>(e);
    case Comp::Material:
        return reg.try_get<Material>(e);
    case Comp::Camera:
        return reg.try_get<Camera>(e);
    case Comp::PointLight:
        return reg.try_get<PointLight>(e);
    }
    return nullptr;
}

void removeC(entt::registry& reg, entt::entity e, Comp c) {
    switch (c) {
    case Comp::Transform:
        reg.remove<Transform>(e);
        break;
    case Comp::Material:
        reg.remove<Material>(e);
        break;
    case Comp::Camera:
        reg.remove<Camera>(e);
        break;
    case Comp::PointLight:
        reg.remove<PointLight>(e);
        break;
    }
}

// write one named field (struct-form vocabulary: "quat" = the 4-float quaternion on Transform,
// everything else a reflect.hpp field name)
bool writeField(Comp c, void* b, const CompField& f, std::string& err) {
    if (c == Comp::Transform && f.name == "quat") {
        if (f.n != 4) {
            err = "quat expects 4 values";
            return false;
        }
        float* q = (float*)((char*)b + offsetof(Transform, quat));
        for (int i = 0; i < 4; ++i)
            q[i] = f.v[i];
        return true;
    }
    const auto* t = refl::findType(compName(c));
    for (const auto& d : t->fields) {
        if (d.name != f.name)
            continue;
        char* dst = (char*)b + d.offset;
        switch (d.kind) {
        case FieldKind::SliderF:
            if (f.n != 1) {
                err = "field '" + f.name + "' expects 1 value";
                return false;
            }
            *(float*)dst = f.v[0];
            return true;
        case FieldKind::Checkbox:
            if (f.n != 1) {
                err = "field '" + f.name + "' expects 1 value";
                return false;
            }
            *(bool*)dst = f.v[0] != 0.0f;
            return true;
        case FieldKind::SliderI:
            if (f.n != 1) {
                err = "field '" + f.name + "' expects 1 value";
                return false;
            }
            *(int*)dst = (int)f.v[0];
            return true;
        case FieldKind::Color:
        case FieldKind::DragV3:
            if (f.n != 3) {
                err = "field '" + f.name + "' expects 3 values";
                return false;
            }
            std::memcpy(dst, f.v, 12);
            return true;
        case FieldKind::Ref:
            err = "field '" + f.name + "' is read-only";
            return false;
        }
    }
    err = "no field '" + f.name + "' in " + compName(c);
    return false;
}

bool createOrReplace(entt::registry& reg, entt::entity e, const std::string& typeName, const CompFields& fields,
                     bool strict, std::string& err) {
    const Comp c = compFor(typeName);
    if (c == Comp::None) {
        err = refl::findType(typeName.c_str())
                  ? "component '" + typeName + "' is created by its verb (soft.body, physics.body, ...)"
                  : "unknown component '" + typeName + "'";
        return false;
    }
    if (strict && present(reg, e, c)) {
        err = "component '" + typeName + "' already present on the entity";
        return false;
    }
    if (!present(reg, e, c))
        emplaceDefault(reg, e, c);
    void* b = base(reg, e, c);
    for (const auto& f : fields)
        if (!writeField(c, b, f, err))
            return false;
    return true;
}

} // namespace

bool compEmplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err) {
    return createOrReplace(reg, e, type, fields, true, err);
}

bool compReplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err) {
    return createOrReplace(reg, e, type, fields, false, err);
}

bool compGet(entt::registry& reg, entt::entity e, const std::string& type, CompFields& out) {
    const Comp c = compFor(type);
    if (c == Comp::None)
        return false;
    void* b = base(reg, e, c);
    if (!b)
        return false;
    if (c == Comp::Transform) {
        const auto* t = (const Transform*)b;
        out.push_back(CompField{"pos", {t->pos.x, t->pos.y, t->pos.z}, 3});
        out.push_back(CompField{"quat", {t->quat.x, t->quat.y, t->quat.z, t->quat.w}, 4});
        return true;
    }
    const auto* t = refl::findType(compName(c));
    for (const auto& d : t->fields) {
        if (d.kind == FieldKind::Ref)
            continue;
        CompField f{d.name, {}, 0};
        const float* src = (const float*)((const char*)b + d.offset);
        switch (d.kind) {
        case FieldKind::SliderF:
            f.n = 1;
            f.v[0] = src[0];
            break;
        case FieldKind::Checkbox:
            f.n = 1;
            f.v[0] = *(const bool*)src ? 1.0f : 0.0f;
            break;
        case FieldKind::SliderI:
            f.n = 1;
            f.v[0] = (float)*(const int*)src;
            break;
        case FieldKind::Color:
        case FieldKind::DragV3:
            f.n = 3;
            for (int i = 0; i < 3; ++i)
                f.v[i] = src[i];
            break;
        case FieldKind::Ref:
            break;
        }
        out.push_back(std::move(f));
    }
    return true;
}

bool compRemove(entt::registry& reg, entt::entity e, const std::string& type, std::string& err) {
    const Comp c = compFor(type);
    if (c == Comp::None) {
        err = refl::findType(type.c_str())
                  ? "component '" + type + "' is created by its verb (soft.body, physics.body, ...)"
                  : "unknown component '" + type + "'";
        return false;
    }
    if (!present(reg, e, c)) {
        err = "component '" + type + "' not present on the entity";
        return false;
    }
    removeC(reg, e, c);
    return true;
}
