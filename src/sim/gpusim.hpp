#pragma once
#include <vulkan/vulkan.h>

#include "sim/skin.hpp"
#include "sim/soft.hpp"

class GpuSim {
public:
    Skin skin;
    Soft soft;

    void init(VkDevice dev, VkPhysicalDevice pdev) {
        skin.init(dev, pdev);
        soft.init(dev, pdev);
    }
    void build() {
        soft.build();
        skin.build();
    }
    void shutdown() {
        soft.shutdown();
        skin.shutdown();
    }
};
