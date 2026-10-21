#pragma once
#include <vulkan/vulkan.h>

#include "sim/skin.hpp"
#include "sim/soft.hpp"
#include <entt/entt.hpp>

class GpuSim {
public:
    Skin skin;
    Soft soft;

    void init(VkDevice dev, VkPhysicalDevice pdev) {
        skin.init(dev, pdev);
        soft.init(dev, pdev);
    }
    void build(const entt::registry& reg) {
        soft.build();
        skin.build(reg);
    }
    void shutdown() {
        soft.shutdown();
        skin.shutdown();
    }
};
