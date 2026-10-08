#pragma once
// hand-written reflection tables: the field knowledge the UI editor and URL resolver both use.
// add a component here to make it inspectable/bindable; omission = not exposed (curation).
#include "world.hpp"
#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

enum class FieldKind { SliderF, SliderI, Checkbox, Color, DragV3 };

struct FieldDesc {
    const char* name;
    ptrdiff_t offset;
    FieldKind kind;
    float lo = 0.0f, hi = 1.0f;
};

struct TypeDesc {
    const char* name;
    std::span<const FieldDesc> fields;
};

namespace refl {

#define OFF(T, f) offsetof(T, f)
#define OFF2(T, a, b) (offsetof(T, a) + offsetof(std::remove_cv_t<decltype(std::declval<T&>().a)>, b))

static const FieldDesc kTransform[] = {
    {"pos", OFF(Transform, pos), FieldKind::DragV3},
};
static const FieldDesc kMaterial[] = {
    {"baseColor", OFF(Material, baseColor), FieldKind::Color},
    {"metallic", OFF(Material, metallic), FieldKind::SliderF, 0.0f, 1.0f},
    {"roughness", OFF(Material, roughness), FieldKind::SliderF, 0.0f, 1.0f},
};
static const FieldDesc kCamera[] = {
    {"fovDeg", OFF(Camera, fovDeg), FieldKind::SliderF, 10.0f, 120.0f},
    {"nearP", OFF(Camera, nearP), FieldKind::SliderF, 0.01f, 5.0f},
    {"farP", OFF(Camera, farP), FieldKind::SliderF, 10.0f, 1000.0f},
    {"active", OFF(Camera, active), FieldKind::Checkbox},
};
static const FieldDesc kPointLight[] = {
    {"on", OFF(PointLight, on), FieldKind::SliderF, 0.0f, 1.0f},
    {"color", OFF(PointLight, color), FieldKind::Color},
    {"intensity", OFF(PointLight, intensity), FieldKind::SliderF, 0.0f, 1000.0f},
    {"radius", OFF(PointLight, radius), FieldKind::SliderF, 0.0f, 2.0f},
    {"shadowNear", OFF(PointLight, shadowNear), FieldKind::SliderF, 0.001f, 1.0f},
    {"shadowFar", OFF(PointLight, shadowFar), FieldKind::SliderF, 1.0f, 100.0f},
    {"shadowNormalBias", OFF(PointLight, shadowNormalBias), FieldKind::SliderF, 0.0f, 1.0f},
    {"shadowBiasBase", OFF(PointLight, shadowBiasBase), FieldKind::SliderF, 0.0f, 2.0f},
    {"shadowBiasSlope", OFF(PointLight, shadowBiasSlope), FieldKind::SliderF, 0.0f, 4.0f},
    {"shadowSearchScale", OFF(PointLight, shadowSearchScale), FieldKind::SliderF, 0.5f, 16.0f},
    {"shadowMaxRadius", OFF(PointLight, shadowMaxRadius), FieldKind::SliderF, 1.0f, 64.0f},
};
static const FieldDesc kSoftBodyData[] = {
    {"pinned", OFF(SoftBodyData, pinned), FieldKind::SliderI, 0.0f, 2.0f},
    {"steps", OFF(SoftBodyData, steps), FieldKind::SliderI, 1.0f, 32.0f},
    {"params.mass", OFF2(SoftBodyData, params, mass), FieldKind::SliderF, 0.1f, 10.0f},
    {"params.damping", OFF2(SoftBodyData, params, damping), FieldKind::SliderF, 0.9f, 1.0f},
    {"params.passes", OFF2(SoftBodyData, params, passes), FieldKind::SliderI, 1.0f, 16.0f},
    {"params.stiffness", OFF2(SoftBodyData, params, stiffness), FieldKind::SliderF, 0.0f, 1.0f},
    {"params.tension", OFF2(SoftBodyData, params, tension), FieldKind::SliderF, 0.5f, 1.5f},
    {"params.friction", OFF2(SoftBodyData, params, friction), FieldKind::SliderF, 0.0f, 50.0f},
};
static const FieldDesc kRigidDynamics[] = {
    {"props.friction", OFF2(RigidDynamics, props, friction), FieldKind::SliderF, 0.0f, 1.0f},
    {"props.restitution", OFF2(RigidDynamics, props, restitution), FieldKind::SliderF, 0.0f, 1.0f},
    {"props.damping", OFF2(RigidDynamics, props, damping), FieldKind::SliderF, 0.0f, 1.0f},
};
static const FieldDesc kRenderSettings[] = {
    {"bgColor", OFF(RenderSettings, bgColor), FieldKind::Color},
    {"exposure", OFF(RenderSettings, exposure), FieldKind::SliderF, 0.1f, 3.0f},
    {"envIntensity", OFF(RenderSettings, envIntensity), FieldKind::SliderF, 0.0f, 3.0f},
};
static const FieldDesc kRigidScene[] = {
    // RigidScene is not standard-layout (Jolt members); velocitySteps is its first member
    {"velocitySteps", 0, FieldKind::SliderI, 1.0f, 64.0f},
};

static const TypeDesc kTypes[] = {
    {"Transform", kTransform},
    {"Material", kMaterial},
    {"Camera", kCamera},
    {"PointLight", kPointLight},
    {"SoftBodyData", kSoftBodyData},
    {"RigidDynamics", kRigidDynamics},
    {"RenderSettings", kRenderSettings},
    {"RigidScene", kRigidScene},
};

inline const TypeDesc* findType(const char* name) {
    for (const auto& t : kTypes)
        if (std::strcmp(t.name, name) == 0)
            return &t;
    return nullptr;
}

// reflected components present on an entity: (type, base pointer)
inline std::vector<std::pair<const TypeDesc*, const void*>> entityFields(const entt::registry& reg, entt::entity e) {
    std::vector<std::pair<const TypeDesc*, const void*>> out;
    if (auto* p = reg.try_get<Transform>(e))
        out.push_back({findType("Transform"), (const void*)p});
    if (auto* p = reg.try_get<Material>(e))
        out.push_back({findType("Material"), (const void*)p});
    if (auto* p = reg.try_get<Camera>(e))
        out.push_back({findType("Camera"), (const void*)p});
    if (auto* p = reg.try_get<PointLight>(e))
        out.push_back({findType("PointLight"), (const void*)p});
    if (auto* p = reg.try_get<SoftBodyData>(e))
        out.push_back({findType("SoftBodyData"), (const void*)p});
    if (auto* p = reg.try_get<RigidDynamics>(e))
        out.push_back({findType("RigidDynamics"), (const void*)p});
    return out;
}

// World-level (non-entity) state exposed under reserved URL roots
struct RootRef {
    const TypeDesc* type;
    void* base;
};

inline std::vector<RootRef> roots(World& w) {
    return {{findType("RenderSettings"), &w.render}, {findType("RigidScene"), &w.rigid}};
}

} // namespace refl
