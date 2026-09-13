#pragma once
#include <vulkan/vulkan.h>

#include <vector>

struct SoftDraw {
    VkBuffer vertices = VK_NULL_HANDLE;
    VkDeviceSize vertexBytes = 0;
    std::vector<uint32_t> indices;
};
