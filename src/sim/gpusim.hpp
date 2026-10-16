#pragma once
#include <vulkan/vulkan.h>

#include "sim/skin.hpp"
#include "sim/soft.hpp"

// the GPU simulation: the two compute systems that produce vertex data on the GPU —
// dual-quaternion skeleton skinning (skin) and position-based soft bodies (soft). The CPU
// rigid sim (RigidScene/Jolt) is a separate, non-GPU module. Owns the two systems and their
// shared lifecycle; the per-entity state lives in the ECS, keyed by the int handles each
// system hands back.
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
