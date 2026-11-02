#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>

struct VertexStore {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t count = 0;
};

struct MeshGpu {
    VertexStore pos;
    VertexStore nrm;
};

// a renderer vertex slot: slot index + read-write vertex stores (sim writes, renderer reads)
struct GpuMeshRef {
    int slot = -1;
    MeshGpu rw;
};
