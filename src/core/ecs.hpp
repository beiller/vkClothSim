#pragma once
#include "components.hpp"

inline entt::entity findNamedEntity(const entt::registry& reg, const std::string& name) {
    for (auto [e, n] : reg.view<Name>().each())
        if (n.id == name)
            return e;
    return entt::null;
}

inline Transform worldTransform(const entt::registry& reg, entt::entity e) {
    Transform acc{};
    bool have = false;
    while (e != entt::null) {
        const Transform& t = reg.get<Transform>(e);
        acc = have ? transformCompose(t, acc) : t;
        have = true;
        const auto* p = reg.try_get<Parent>(e);
        e = p ? p->e : entt::null;
    }
    return acc;
}
