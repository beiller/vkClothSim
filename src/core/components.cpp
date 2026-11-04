#include "components.hpp"
#include "ecs.hpp"

#include "reflect.hpp"
#include <cstring>

namespace {

enum class Cmp {
    Transform,
    Material,
    MaterialRef,
    Camera,
    Environment,
    PointLight,
    SoftBodyData,
    RigidBody,
    KinematicBody,
    CapsuleShape,
    BoxShape,
    TaperedCapsule,
    SphereShape,
    ConvexHullShape,
    Collider,
    ControllerInput,
    MouseInput,
    Renderable,
    Texture,
    Geometry,
    Skeleton,
    Joint,
    SixDofJoint,
    Parent,
    Bind,
    None,
};

const char* cmpName(Cmp c) {
    switch (c) {
    case Cmp::Transform:
        return "Transform";
    case Cmp::Material:
        return "Material";
    case Cmp::MaterialRef:
        return "MaterialRef";
    case Cmp::Camera:
        return "Camera";
    case Cmp::Environment:
        return "Environment";
    case Cmp::PointLight:
        return "PointLight";
    case Cmp::SoftBodyData:
        return "SoftBodyData";
    case Cmp::RigidBody:
        return "RigidBody";
    case Cmp::KinematicBody:
        return "KinematicBody";
    case Cmp::CapsuleShape:
        return "CapsuleShape";
    case Cmp::BoxShape:
        return "BoxShape";
    case Cmp::TaperedCapsule:
        return "TaperedCapsule";
    case Cmp::SphereShape:
        return "SphereShape";
    case Cmp::ConvexHullShape:
        return "ConvexHullShape";
    case Cmp::Collider:
        return "Collider";
    case Cmp::ControllerInput:
        return "ControllerInput";
    case Cmp::MouseInput:
        return "MouseInput";
    case Cmp::Renderable:
        return "Renderable";
    case Cmp::Texture:
        return "Texture";
    case Cmp::Geometry:
        return "Geometry";
    case Cmp::Skeleton:
        return "Skeleton";
    case Cmp::Joint:
        return "Joint";
    case Cmp::SixDofJoint:
        return "SixDofJoint";
    case Cmp::Parent:
        return "Parent";
    case Cmp::Bind:
        return "Bind";
    }
    return nullptr;
}

Cmp cmpFor(const std::string& name) {
    for (int i = 0; i < (int)Cmp::None; ++i) {
        const char* n = cmpName((Cmp)i);
        if (name == n)
            return (Cmp)i;
    }
    return Cmp::None;
}

void* base(const entt::registry& reg, entt::entity e, Cmp c) {
    switch (c) {
    case Cmp::Transform:
        return const_cast<Transform*>(reg.try_get<Transform>(e));
    case Cmp::Material:
        return const_cast<Material*>(reg.try_get<Material>(e));
    case Cmp::MaterialRef:
        return const_cast<MaterialRef*>(reg.try_get<MaterialRef>(e));
    case Cmp::Camera:
        return const_cast<Camera*>(reg.try_get<Camera>(e));
    case Cmp::Environment:
        return const_cast<Environment*>(reg.try_get<Environment>(e));
    case Cmp::PointLight:
        return const_cast<PointLight*>(reg.try_get<PointLight>(e));
    case Cmp::SoftBodyData:
        return const_cast<SoftBodyData*>(reg.try_get<SoftBodyData>(e));
    case Cmp::RigidBody:
        return const_cast<RigidBody*>(reg.try_get<RigidBody>(e));
    case Cmp::KinematicBody:
        return const_cast<KinematicBody*>(reg.try_get<KinematicBody>(e));
    case Cmp::CapsuleShape:
        return const_cast<CapsuleShape*>(reg.try_get<CapsuleShape>(e));
    case Cmp::BoxShape:
        return const_cast<BoxShape*>(reg.try_get<BoxShape>(e));
    case Cmp::TaperedCapsule:
        return const_cast<TaperedCapsule*>(reg.try_get<TaperedCapsule>(e));
    case Cmp::SphereShape:
        return const_cast<SphereShape*>(reg.try_get<SphereShape>(e));
    case Cmp::ConvexHullShape:
        return const_cast<ConvexHullShape*>(reg.try_get<ConvexHullShape>(e));
    case Cmp::Collider:
        return const_cast<Collider*>(reg.try_get<Collider>(e));
    case Cmp::ControllerInput:
        return const_cast<ControllerInput*>(reg.try_get<ControllerInput>(e));
    case Cmp::MouseInput:
        return const_cast<MouseInput*>(reg.try_get<MouseInput>(e));
    case Cmp::Renderable:
        return const_cast<Renderable*>(reg.try_get<Renderable>(e));
    case Cmp::Texture:
        return const_cast<Texture*>(reg.try_get<Texture>(e));
    case Cmp::Geometry:
        return const_cast<Geometry*>(reg.try_get<Geometry>(e));
    case Cmp::Skeleton:
        return const_cast<Skeleton*>(reg.try_get<Skeleton>(e));
    case Cmp::Joint:
        return const_cast<Joint*>(reg.try_get<Joint>(e));
    case Cmp::SixDofJoint:
        return const_cast<SixDofJoint*>(reg.try_get<SixDofJoint>(e));
    case Cmp::Parent:
        return const_cast<Parent*>(reg.try_get<Parent>(e));
    case Cmp::Bind:
        return const_cast<Bind*>(reg.try_get<Bind>(e));
    }
    return nullptr;
}

bool hasComp(const entt::registry& reg, entt::entity e, Cmp c) {
    return base(reg, e, c) != nullptr;
}

void emplaceDefault(entt::registry& reg, entt::entity e, Cmp c) {
    switch (c) {
    case Cmp::Transform:
        reg.emplace<Transform>(e);
        break;
    case Cmp::Material:
        reg.emplace<Material>(e);
        break;
    case Cmp::MaterialRef:
        reg.emplace<MaterialRef>(e);
        break;
    case Cmp::Camera:
        reg.emplace<Camera>(e);
        break;
    case Cmp::Environment:
        reg.emplace<Environment>(e);
        break;
    case Cmp::PointLight:
        reg.emplace<PointLight>(e);
        break;
    case Cmp::SoftBodyData:
        reg.emplace<SoftBodyData>(e);
        break;
    case Cmp::RigidBody:
        reg.emplace<RigidBody>(e);
        break;
    case Cmp::KinematicBody:
        reg.emplace<KinematicBody>(e);
        break;
    case Cmp::CapsuleShape:
        reg.emplace<CapsuleShape>(e);
        break;
    case Cmp::BoxShape:
        reg.emplace<BoxShape>(e);
        break;
    case Cmp::TaperedCapsule:
        reg.emplace<TaperedCapsule>(e);
        break;
    case Cmp::SphereShape:
        reg.emplace<SphereShape>(e);
        break;
    case Cmp::ConvexHullShape:
        reg.emplace<ConvexHullShape>(e);
        break;
    case Cmp::Collider:
        reg.emplace<Collider>(e);
        break;
    case Cmp::ControllerInput:
        reg.emplace<ControllerInput>(e);
        break;
    case Cmp::MouseInput:
        reg.emplace<MouseInput>(e);
        break;
    case Cmp::Renderable:
        reg.emplace<Renderable>(e);
        break;
    case Cmp::Texture:
        reg.emplace<Texture>(e);
        break;
    case Cmp::Geometry:
        reg.emplace<Geometry>(e);
        break;
    case Cmp::Skeleton:
        reg.emplace<Skeleton>(e);
        break;
    case Cmp::Joint:
        reg.emplace<Joint>(e);
        break;
    case Cmp::SixDofJoint:
        reg.emplace<SixDofJoint>(e);
        break;
    case Cmp::Parent:
        reg.emplace<Parent>(e);
        break;
    case Cmp::Bind:
        reg.emplace<Bind>(e);
        break;
    }
}

void removeC(entt::registry& reg, entt::entity e, Cmp c) {
    switch (c) {
    case Cmp::Transform:
        reg.remove<Transform>(e);
        break;
    case Cmp::Material:
        reg.remove<Material>(e);
        break;
    case Cmp::MaterialRef:
        reg.remove<MaterialRef>(e);
        break;
    case Cmp::Camera:
        reg.remove<Camera>(e);
        break;
    case Cmp::Environment:
        reg.remove<Environment>(e);
        break;
    case Cmp::PointLight:
        reg.remove<PointLight>(e);
        break;
    case Cmp::SoftBodyData:
        reg.remove<SoftBodyData>(e);
        break;
    case Cmp::RigidBody:
        reg.remove<RigidBody>(e);
        break;
    case Cmp::KinematicBody:
        reg.remove<KinematicBody>(e);
        break;
    case Cmp::CapsuleShape:
        reg.remove<CapsuleShape>(e);
        break;
    case Cmp::BoxShape:
        reg.remove<BoxShape>(e);
        break;
    case Cmp::TaperedCapsule:
        reg.remove<TaperedCapsule>(e);
        break;
    case Cmp::SphereShape:
        reg.remove<SphereShape>(e);
        break;
    case Cmp::ConvexHullShape:
        reg.remove<ConvexHullShape>(e);
        break;
    case Cmp::Collider:
        reg.remove<Collider>(e);
        break;
    case Cmp::ControllerInput:
        reg.remove<ControllerInput>(e);
        break;
    case Cmp::MouseInput:
        reg.remove<MouseInput>(e);
        break;
    case Cmp::Renderable:
        reg.remove<Renderable>(e);
        break;
    case Cmp::Texture:
        reg.remove<Texture>(e);
        break;
    case Cmp::Geometry:
        reg.remove<Geometry>(e);
        break;
    case Cmp::Skeleton:
        reg.remove<Skeleton>(e);
        break;
    case Cmp::Joint:
        reg.remove<Joint>(e);
        break;
    case Cmp::SixDofJoint:
        reg.remove<SixDofJoint>(e);
        break;
    case Cmp::Parent:
        reg.remove<Parent>(e);
        break;
    case Cmp::Bind:
        reg.remove<Bind>(e);
        break;
    }
}

bool fieldValue(const CompField& f, const float*& src, int& len) {
    if (f.isArr) {
        if (f.arr.empty()) {
            src = f.v;
            len = f.n;
        } else {
            src = f.arr.data();
            len = (int)f.arr.size();
        }
        return true;
    }
    if (f.n > 0) {
        src = f.v;
        len = f.n;
        return true;
    }
    return false;
}

bool checkShape(const CompField& f, const std::string& want, std::string& err) {
    if (f.isStr) {
        err = "field '" + f.name + "' expects " + want + ", got a string";
        return false;
    }
    return true;
}

bool writeField(const TypeDesc* t, void* b, const CompField& f, std::string& err) {
    const FieldDesc* d = findField(t, f.name);
    if (!d) {
        err = "no field '" + f.name + "' in " + t->name;
        return false;
    }
    char* dst = (char*)b + d->offset;
    const float* src = nullptr;
    int len = 0;
    switch (d->kind) {
    case FieldKind::SliderF:
    case FieldKind::SliderI:
    case FieldKind::Checkbox: {
        if (!checkShape(f, "1 value", err) || f.isArr) {
            err = "field '" + f.name + "' expects 1 value";
            return false;
        }
        if (f.n != 1) {
            err = "field '" + f.name + "' expects 1 value, got " + std::to_string(f.n);
            return false;
        }
        switch (d->kind) {
        case FieldKind::SliderF:
            *(float*)dst = f.v[0];
            break;
        case FieldKind::SliderI:
            *(int*)dst = (int)f.v[0];
            break;
        case FieldKind::Checkbox:
            *(bool*)dst = f.v[0] != 0.0f;
            break;
        }
        return true;
    }
    case FieldKind::Color:
    case FieldKind::DragV3:
    case FieldKind::Quat: {
        const int want = d->kind == FieldKind::Quat ? 4 : 3;
        if (!checkShape(f, std::to_string(want) + " values", err) || !fieldValue(f, src, len) ||
            len != want) {
            err = "field '" + f.name + "' expects " + std::to_string(want) + " values, got " +
                  std::to_string(len);
            return false;
        }
        std::memcpy(dst, src, (size_t)want * 4);
        return true;
    }
    case FieldKind::Ref: {
        if (!f.hasEnt) {
            err = "field '" + f.name + "' expects an entity reference";
            return false;
        }
        *(entt::entity*)dst = f.ent;
        return true;
    }
    case FieldKind::Str: {
        if (!f.isStr) {
            err = "field '" + f.name + "' expects a string";
            return false;
        }
        *(std::string*)dst = f.str;
        return true;
    }
    case FieldKind::StrArr: {
        if (!f.isStrArr) {
            err = "field '" + f.name + "' expects an array of strings";
            return false;
        }
        *(std::vector<std::string>*)dst = f.strArr;
        return true;
    }
    case FieldKind::FloatArr: {
        if (!f.isArr) {
            err = "field '" + f.name + "' expects an array of numbers";
            return false;
        }
        *(std::vector<float>*)dst = f.arr.empty() ? std::vector<float>(f.v, f.v + f.n) : f.arr;
        return true;
    }
    case FieldKind::UIntArr: {
        if (!f.isArr) {
            err = "field '" + f.name + "' expects an array of numbers";
            return false;
        }
        const std::vector<float> src = f.arr.empty() ? std::vector<float>(f.v, f.v + f.n) : f.arr;
        auto& dstArr = *(std::vector<uint32_t>*)dst;
        dstArr.resize(src.size());
        for (size_t i = 0; i < src.size(); ++i)
            dstArr[i] = (uint32_t)src[i]; // exact for values < 2^24
        return true;
    }
    case FieldKind::EntArr: {
        if (!f.isArr) {
            err = "field '" + f.name + "' expects an array of entity handles";
            return false;
        }
        const std::vector<float> src = f.arr.empty() ? std::vector<float>(f.v, f.v + f.n) : f.arr;
        auto& dstArr = *(std::vector<entt::entity>*)dst;
        dstArr.resize(src.size());
        for (size_t i = 0; i < src.size(); ++i)
            dstArr[i] = (entt::entity)src[i];
        return true;
    }
    }
    return true;
}

bool validateGeometry(entt::registry& reg, entt::entity e, std::string& err) {
    auto& g = reg.get<Geometry>(e).mesh;
    const int nv = g.vertexCount();
    if (nv < 0) {
        err = "Geometry: pos must be a multiple of 3";
        return false;
    }
    if (g.nrm.empty())
        g.nrm.assign((size_t)nv * 3, 0.0f);
    if (g.uv.empty())
        g.uv.assign((size_t)nv * 2, 0.0f);
    if (g.nrm.size() != (size_t)nv * 3) {
        err = "Geometry: nrm must have 3 values per vertex";
        return false;
    }
    if (g.uv.size() != (size_t)nv * 2) {
        err = "Geometry: uv must have 2 values per vertex";
        return false;
    }
    for (uint32_t i : g.indices)
        if (i >= (uint32_t)nv) {
            err = "Geometry: index out of range";
            return false;
        }
    if (!g.joints.empty() || !g.weights.empty()) {
        if (g.joints.size() != (size_t)4 * nv || g.weights.size() != (size_t)4 * nv) {
            err = "Geometry: joints and weights must each have 4 values per vertex";
            return false;
        }
    }
    for (const auto& sh : g.shapes)
        if (!sh.pos.empty() && sh.pos.size() != (size_t)3 * nv) {
            err = "Geometry: shape pos must have 3 values per vertex";
            return false;
        }
    return true;
}

// re-snapshot the bind pose on every Skeleton write: invBinds = the inverse of
// each joint's current world transform (re-writing the component re-binds)
bool bindSkeleton(entt::registry& reg, entt::entity e, std::string& err) {
    auto& s = reg.get<Skeleton>(e);
    if (!s.joints.empty() && s.joints.size() != s.boneNames.size()) {
        err = "Skeleton: joints and boneNames must have the same count";
        return false;
    }
    s.invBinds.clear();
    for (size_t i = 0; i < s.joints.size(); ++i) {
        const entt::entity j = s.joints[i];
        if (!reg.valid(j) || !reg.try_get<Transform>(j)) {
            err = "Skeleton: joint " + std::to_string(i + 1) + " has no Transform";
            return false;
        }
        s.invBinds.push_back(glm::inverse(worldTransform(reg, j).toMat4()));
    }
    return true;
}

bool createOrReplace(entt::registry& reg, entt::entity e, const std::string& typeName, const CompFields& fields,
                     bool strict, std::string& err) {
    const Cmp c = cmpFor(typeName);
    if (c == Cmp::None) {
        err = refl::findType(typeName.c_str())
                  ? "component '" + typeName + "' is engine state, not an entity component"
                  : "unknown component '" + typeName + "'";
        return false;
    }
    const TypeDesc* t = refl::findType(cmpName(c));
    if (strict && hasComp(reg, e, c)) {
        err = "component '" + typeName + "' already present on the entity";
        return false;
    }
    if (!hasComp(reg, e, c))
        emplaceDefault(reg, e, c);
    void* b = base(reg, e, c);
    for (auto f : fields) {
        if (const FieldDesc* d = findField(t, f.name)) {
            if (f.isStr && d->kind == FieldKind::Ref) {
                const entt::entity r = findNamedEntity(reg, f.str);
                if (r == entt::null) {
                    err = "field '" + f.name + "': unknown entity '" + f.str + "' (define it before use)";
                    return false;
                }
                f.isStr = false;
                f.hasEnt = true;
                f.ent = r;
            }
            if (f.isStrArr && d->kind == FieldKind::EntArr) {
                f.arr.clear();
                for (const std::string& s : f.strArr) {
                    const entt::entity r = findNamedEntity(reg, s);
                    if (r == entt::null) {
                        err = "field '" + f.name + "': unknown entity '" + s + "' (define it before use)";
                        return false;
                    }
                    f.arr.push_back((float)r);
                }
                f.isStrArr = false;
                f.isArr = true;
            }
        }
        if (!writeField(t, b, f, err))
            return false;
    }
    if (c == Cmp::Geometry && !validateGeometry(reg, e, err))
        return false;
    if (c == Cmp::Skeleton && !bindSkeleton(reg, e, err))
        return false;
    if (c == Cmp::TaperedCapsule) {
        const auto& tc = reg.get<TaperedCapsule>(e);
        if (tc.bottomRadius <= 0.0f || tc.topRadius <= 0.0f || tc.halfHeight < 0.0f) {
            err = "TaperedCapsule: radii must be > 0 and halfHeight >= 0";
            return false;
        }
    }
    if (c == Cmp::SphereShape) {
        if (reg.get<SphereShape>(e).radius <= 0.0f) {
            err = "SphereShape: radius must be > 0";
            return false;
        }
    }
    if (c == Cmp::ConvexHullShape) {
        if (reg.get<ConvexHullShape>(e).points.size() < 12) {
            err = "ConvexHullShape: needs at least 4 points (12 xyz values)";
            return false;
        }
    }
    return true;
}

}

bool hasComponentByName(const entt::registry& reg, entt::entity e, const std::string& name) {
    return hasComp(reg, e, cmpFor(name));
}

bool compEmplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err) {
    return createOrReplace(reg, e, type, fields, true, err);
}

bool compReplace(entt::registry& reg, entt::entity e, const std::string& type, const CompFields& fields,
                 std::string& err) {
    return createOrReplace(reg, e, type, fields, false, err);
}

bool compGet(const entt::registry& reg, entt::entity e, const std::string& type, CompFields& out) {
    const Cmp c = cmpFor(type);
    if (c == Cmp::None)
        return false;
    if (!hasComp(reg, e, c))
        return false;
    void* b = base(reg, e, c);
    const TypeDesc* t = refl::findType(cmpName(c));
    for (const auto& d : t->fields) {
        const char* src = (const char*)b + d.offset;
        CompField f{d.name, {}, 0};
        switch (d.kind) {
        case FieldKind::SliderF:
            f.n = 1;
            f.v[0] = *(const float*)src;
            break;
        case FieldKind::SliderI:
            f.n = 1;
            f.v[0] = (float)*(const int*)src;
            break;
        case FieldKind::Checkbox:
            f.n = 1;
            f.v[0] = *(const bool*)src ? 1.0f : 0.0f;
            break;
        case FieldKind::Color:
        case FieldKind::DragV3:
            f.n = 3;
            std::memcpy(f.v, src, 12);
            break;
        case FieldKind::Quat:
            f.n = 4;
            std::memcpy(f.v, src, 16);
            break;
        case FieldKind::Ref:
            f.n = 1;
            f.hasEnt = true;
            f.ent = *(const entt::entity*)src;
            f.v[0] = (float)f.ent;
            break;
        case FieldKind::Str:
            f.isStr = true;
            f.str = *(const std::string*)src;
            break;
        case FieldKind::StrArr: {
            f.isStrArr = true;
            f.strArr = *(const std::vector<std::string>*)src;
            f.n = (int)f.strArr.size();
            break;
        }
        case FieldKind::FloatArr:
            f.isArr = true;
            f.arr = *(const std::vector<float>*)src;
            f.n = (int)f.arr.size();
            break;
        case FieldKind::UIntArr: {
            f.isArr = true;
            const auto& a = *(const std::vector<uint32_t>*)src;
            f.arr.resize(a.size());
            for (size_t i = 0; i < a.size(); ++i)
                f.arr[i] = (float)a[i];
            f.n = (int)f.arr.size();
            break;
        }
        case FieldKind::EntArr: {
            f.isArr = true;
            const auto& a = *(const std::vector<entt::entity>*)src;
            f.arr.resize(a.size());
            for (size_t i = 0; i < a.size(); ++i)
                f.arr[i] = (float)a[i];
            f.n = (int)f.arr.size();
            break;
        }
        }
        out.push_back(std::move(f));
    }
    return true;
}

bool compRemove(entt::registry& reg, entt::entity e, const std::string& type, std::string& err) {
    const Cmp c = cmpFor(type);
    if (c == Cmp::None) {
        err = refl::findType(type.c_str())
                  ? "component '" + type + "' is engine state, not an entity component"
                  : "unknown component '" + type + "'";
        return false;
    }
    if (!hasComp(reg, e, c)) {
        err = "component '" + type + "' not present on the entity";
        return false;
    }
    removeC(reg, e, c);
    return true;
}
