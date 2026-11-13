#include "components.hpp"

#include "anim.hpp"
#include "ecs.hpp"
#include "reflect.hpp"
#include "url.hpp"
#include <cstring>
#include <span>

namespace {

template <typename T> struct CompT {
    static void* tryGet(const entt::registry& reg, entt::entity e) { return const_cast<T*>(reg.try_get<T>(e)); }
    static void emplace(entt::registry& reg, entt::entity e) { reg.emplace<T>(e); }
    static void remove(entt::registry& reg, entt::entity e) { reg.remove<T>(e); }
};
} // namespace

static bool validateTaperedCapsule(entt::registry& reg, entt::entity e, std::string& err) {
    const auto& tc = reg.get<TaperedCapsule>(e);
    if (tc.bottomRadius <= 0.0f || tc.topRadius <= 0.0f || tc.halfHeight < 0.0f) {
        err = "TaperedCapsule: radii must be > 0 and halfHeight >= 0";
        return false;
    }
    return true;
}

static bool validateSphereShape(entt::registry& reg, entt::entity e, std::string& err) {
    if (reg.get<SphereShape>(e).radius <= 0.0f) {
        err = "SphereShape: radius must be > 0";
        return false;
    }
    return true;
}

static bool validateConvexHullShape(entt::registry& reg, entt::entity e, std::string& err) {
    if (reg.get<ConvexHullShape>(e).points.size() < 12) {
        err = "ConvexHullShape: needs at least 4 points (12 xyz values)";
        return false;
    }
    return true;
}

static bool validateGeometry(entt::registry& reg, entt::entity e, std::string& err);
static bool bindSkeleton(entt::registry& reg, entt::entity e, std::string& err);
static bool animClipAfterWrite(entt::registry& reg, entt::entity e, std::string& err);

#define CMP(T) {#T, refl::findType(#T), &CompT<T>::tryGet, &CompT<T>::emplace, &CompT<T>::remove, nullptr}
#define CMPV(T, V) {#T, refl::findType(#T), &CompT<T>::tryGet, &CompT<T>::emplace, &CompT<T>::remove, &(V)}

static std::span<const CompReg> comps() {
    static const CompReg kComps[] = {
        CMP(Transform),
        CMP(Material),
        CMP(MaterialRef),
        CMP(Camera),
        CMP(Environment),
        CMP(PointLight),
        CMP(SoftBodyData),
        CMP(RigidBody),
        CMP(KinematicBody),
        CMP(CapsuleShape),
        CMP(BoxShape),
        CMPV(TaperedCapsule, validateTaperedCapsule),
        CMPV(SphereShape, validateSphereShape),
        CMPV(ConvexHullShape, validateConvexHullShape),
        CMP(Collider),
        CMP(ControllerInput),
        CMP(MouseInput),
        CMP(Renderable),
        CMP(Texture),
        CMPV(Geometry, validateGeometry),
        CMPV(Skeleton, bindSkeleton),
        CMP(Joint),
        CMP(SixDofJoint),
        CMP(Parent),
        CMP(Bind),
        CMPV(AnimationClip, animClipAfterWrite),
        CMP(AnimationPlayer),
        CMP(UIWidget),
        CMP(UIButton),
        CMP(UICanvas),
        CMP(GPUTexture),
        CMP(UIWindow),
        CMP(UITab),
        CMP(UIText),
        CMP(UISection),
        CMP(UISliderF),
        CMP(UISliderI),
        CMP(UICheckbox),
        CMP(UIColorEdit),
        CMP(UIDragV3),
    };

#undef CMP
#undef CMPV
    return kComps;
}

const CompReg* compFor(const std::string& name) {
    for (const auto& c : comps())
        if (name == c.name)
            return &c;
    return nullptr;
}

static bool fieldValue(const CompField& f, const float*& src, int& len) {
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

static bool checkShape(const CompField& f, const std::string& want, std::string& err) {
    if (f.isStr) {
        err = "field '" + f.name + "' expects " + want + ", got a string";
        return false;
    }
    return true;
}

static bool writeField(const TypeDesc* t, void* b, const CompField& f, std::string& err) {
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
        default:
            break;
        }
        return true;
    }
    case FieldKind::Color:
    case FieldKind::DragV3:
    case FieldKind::Quat: {
        const int want = d->kind == FieldKind::Quat ? 4 : 3;
        if (!checkShape(f, std::to_string(want) + " values", err) || !fieldValue(f, src, len) || len != want) {
            err = "field '" + f.name + "' expects " + std::to_string(want) + " values, got " + std::to_string(len);
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
            dstArr[i] = (uint32_t)src[i];
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
    default:
        break;
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
    if ((!g.joints.empty() || !g.weights.empty()) &&
        (g.joints.size() != (size_t)4 * nv || g.weights.size() != (size_t)4 * nv)) {
        err = "Geometry: joints and weights must each have 4 values per vertex";
        return false;
    }
    for (const auto& sh : g.shapes)
        if (!sh.pos.empty() && sh.pos.size() != (size_t)3 * nv) {
            err = "Geometry: shape pos must have 3 values per vertex";
            return false;
        }
    return true;
}

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

bool animClipAfterWrite(entt::registry& reg, entt::entity e, std::string&) {
    auto& c = reg.get<AnimationClip>(e);
    if (c.duration <= 0.0f)
        c.duration = animDuration(c.trackStart, c.times); // any write may change the keyframes
    return true;
}

static bool createOrReplace(entt::registry& reg, entt::entity e, const std::string& typeName, const CompFields& fields,
                            bool strict, std::string& err) {
    const CompReg* c = compFor(typeName);
    if (!c) {
        err = refl::findType(typeName.c_str()) ? "component '" + typeName + "' is engine state, not an entity component"
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

void setName(entt::registry& reg, entt::entity e, const std::string& id) {
    if (auto* n = reg.try_get<Name>(e)) {
        if (n->id == id)
            return; // unchanged: no remap
        n->id = id; // emplace does not replace in this entt: write in place
    } else {
        reg.emplace<Name>(e, id);
    }
    urlInvalidateAll(); // a new/changed name may remap every cached URL
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

bool compWriteField(entt::registry& reg, entt::entity e, const std::string& typeName, const CompField& f,
                    std::string& err) {
    const CompReg* c = compFor(typeName);
    if (!c) {
        err = refl::findType(typeName.c_str()) ? "component '" + typeName + "' is engine state, not an entity component"
                                               : "unknown component '" + typeName + "'";
        return false;
    }
    void* b = c->tryGet(reg, e);
    if (!b) {
        err = "component '" + typeName + "' is not present on this entity";
        return false;
    }
    if (!writeField(c->desc, b, f, err))
        return false;
    return !c->afterWrite || c->afterWrite(reg, e, err);
}

bool compRemove(entt::registry& reg, entt::entity e, const std::string& type, std::string& err) {
    const CompReg* c = compFor(type);
    if (!c) {
        err = refl::findType(type.c_str()) ? "component '" + type + "' is engine state, not an entity component"
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
