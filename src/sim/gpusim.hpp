#pragma once
#include <vulkan/vulkan.h>

#include "sim/skeleton.hpp"
#include "sim/softsim.hpp"

// the GPU simulation: the two compute systems that produce vertex data on the GPU —
// dual-quaternion skeleton skinning (skel) and position-based soft bodies (soft). The CPU
// rigid sim (RigidScene/Jolt) is a separate, non-GPU module. Owns the two systems and their
// shared lifecycle; the per-entity state lives in the ECS, keyed by the int handles each
// system hands back.
class GpuSim {
public:
    SkelGpu skel;
    SoftSim soft;

    void init(VkDevice dev, VkPhysicalDevice pdev) {
        skel.init(dev, pdev);
        soft.init(dev, pdev);
    }
    void build() {
        soft.build();
        skel.build();
    }
    void shutdown() {
        soft.shutdown();
        skel.shutdown();
    }
};
