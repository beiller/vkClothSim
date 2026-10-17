#pragma once
#include <vulkan/vulkan.h>

#include "sim/skin.hpp"
#include "sim/soft.hpp"

// the GPU simulation: the two compute systems that produce vertex data on the GPU —
// dual-quaternion skeleton skinning (skin) and position-based soft bodies (soft). The CPU
// rigid sim (RigidScene/Jolt) is a separate, non-GPU module. Owns the two systems and their
// shared lifecycle. Each system holds its own entity -> resource handle map (the ECS keeps
// only data); entity destroys are pushed back through the systems' watch(reg) observers.
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
