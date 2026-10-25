#pragma once
#include "ecs.hpp"

inline void addMesh(entt::registry& reg, entt::entity e, entt::entity geom, entt::entity mat, entt::entity parent,
                    bool noShadow = false) {
    if (!reg.try_get<Transform>(e))
        reg.emplace<Transform>(e);
    reg.emplace<Renderable>(e, geom, noShadow);
    reg.emplace<MaterialRef>(e, mat);
    if (parent != entt::null)
        reg.emplace<Parent>(e, parent);
}

inline entt::entity spawnMaterial(entt::registry& reg, const V3& baseColor, float metallic, float roughness,
                                 const V3& emissive, const char* name = nullptr) {
    entt::entity e = reg.create();
    reg.emplace<Material>(e, baseColor, metallic, roughness, emissive);
    if (name)
        reg.emplace<Name>(e, name);
    return e;
}

inline entt::entity makeTexture(entt::registry& reg, const std::string& id, const uint8_t* rgba, int wpx, int hpx) {
    for (auto [e, tx] : reg.view<Texture>().each())
        if (tx.path == id)
            return e;
    entt::entity e = reg.create();
    reg.emplace<Texture>(e, Texture{id, TexelFormat::RGBA8, "linear",
                                   std::vector<uint8_t>(rgba, rgba + (size_t)wpx * (size_t)hpx * 4), (uint32_t)wpx, (uint32_t)hpx});
    reg.emplace<Name>(e, id);
    return e;
}
