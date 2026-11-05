#include "components.hpp"
#include "ecs.hpp"

#include "reflect.hpp"
#include <cstring>

namespace {

struct CompReg {
    const char* name;
    const TypeDesc* desc;
    void* (*tryGet)(const entt::registry&, entt::entity);
    void (*emplace)(entt::registry&, entt::entity);
    void (*remove)(entt::registry&, entt::entity);
    bool (*afterWrite)(entt::registry&, entt::entity, std::string&);
};

template <typename T>
struct CompT {
    static void* tryGet(const entt::registry& reg, entt::entity e) { return const_cast<T*>(reg.try_get<T>(e)); }
    static void emplace(entt::registry& reg, entt::entity e) { reg.emplace<T>(e); }
    static void remove(entt::registry& reg, entt::entity e) { reg.remove<T>(e); }
};

bool validateTaperedCapsule(entt::registry& reg, entt::entity e, std::string& err) {
    const auto& tc = reg.get<TaperedCapsule>(e);
    if (tc.bottomRadius <= 0.0f || tc.topRadius <= 0.0f || tc.halfHeight < 0.0f) {
        err = "TaperedCapsule: radii must be > 0 and halfHeight >= 0";
        return false;
    }
    return true;
}

bool validateSphereShape(entt::registry& reg, entt::entity e, std::string& err) {
    if (reg.get<SphereShape>(e).radius <= 0.0f) {
        err = "SphereShape: radius must be > 0";
        return false;
    }
    return true;
}

bool validateConvexHullShape(entt::registry& reg, entt::entity e, std::string& err) {
    if (reg.get<ConvexHullShape>(e).points.size() < 12) {
        err = "ConvexHullShape: needs at least 4 points (12 xyz values)";
        return false;
    }
    return true;
}

bool validateGeometry(entt::registry& reg, entt::entity e, std::string& err);
bool bindSkeleton(entt::registry& reg, entt::entity e, std::string& err);

#define CMP(T) \
    { #T, refl::findType(#T), &CompT<T>::tryGet, &CompT<T>::emplace, &CompT<T>::remove, nullptr }
#define CMPV(T, V) \
    { #T, refl::findType(#T), &CompT<T>::tryGet, &CompT<T>::emplace, &CompT<T>::remove, &V }

const CompReg kComps[] = {
    CMP(Transform),     CMP(Material),      CMP(MaterialRef),  CMP(Camera),       CMP(Environment),
    CMP(PointLight),    CMP(SoftBodyData), CMP(RigidBody),    CMP(KinematicBody), CMP(CapsuleShape),
    CMP(BoxShape),      CMPV(TaperedCapsule, validateTaperedCapsule), CMPV(SphereShape, validateSphereShape),
    CMPV(ConvexHullShape, validateConvexHullShape), CMP(Collider), CMP(ControllerInput), CMP(MouseInput),
    CMP(Renderable),    CMP(Texture),       CMPV(Geometry, validateGeometry), CMPV(Skeleton, bindSkeleton),
    CMP(Joint),         CMP(SixDofJoint),   CMP(Parent),        CMP(Bind),
};

#undef CMP
#undef CMPV

const CompReg* compFor(const std::string& name) {
    for (const auto& c : kComps)
        if (name == c.name)
            return &c;
    return nullptr;
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
    const CompReg* c = compFor(typeName);
    if (!c) {
        err = refl::findType(typeName.c_str())
                  ? "component '" + typeName + "' is engine state, not an entity component"
                  : "unknown component '" + typeName + "'";
        return false;
    }
    const TypeDesc* t = c->desc;
    void* b = c->tryGet(reg, e);
    if (strict && b) {
        err = "component '" + typeName + "' already present on the entity";
        return false;
    }
    if (!b) {
        c->emplace(reg, e);
        b = c->tryGet(reg, e);
    }
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
    return !c->afterWrite || c->afterWrite(reg, e, err);
}

}

bool hasComponentByName(const entt::registry& reg, entt::entity e, const std::string& name) {
    const CompReg* c = compFor(name);
    return c && c->tryGet(reg, e) != nullptr;
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
    const CompReg* c = compFor(type);
    if (!c)
        return false;
    void* b = c->tryGet(reg, e);
    if (!b)
        return false;
    for (const auto& d : c->desc->fields) {
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
    const CompReg* c = compFor(type);
    if (!c) {
        err = refl::findType(type.c_str())
                  ? "component '" + type + "' is engine state, not an entity component"
                  : "unknown component '" + type + "'";
        return false;
    }
    if (!c->tryGet(reg, e)) {
        err = "component '" + type + "' not present on the entity";
        return false;
    }
    c->remove(reg, e);
    return true;
}
