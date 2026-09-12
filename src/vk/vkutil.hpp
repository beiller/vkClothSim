// vkutil.hpp
// Small Vulkan helpers shared by the window/core (vkapp) and the renderer: the
// error-check macro, memory-type lookup, and buffer creation. No scene/sim/UI knowledge.
#pragma once
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Check a VkResult; print + abort on failure.
#define VK(x)                                                                                                       \
    do {                                                                                                            \
        VkResult _r = (x);                                                                                          \
        if (_r != VK_SUCCESS) {                                                                                     \
            std::fprintf(stderr, "vk error %d @%s:%d\n", (int)_r, __FILE__, __LINE__);                              \
            std::abort();                                                                                           \
        }                                                                                                           \
    } while (0)

// Pick a memory-type index that is in `mr`'s bits and has ALL of `need` set (fallback 0).
inline uint32_t vkFindMemoryType(VkPhysicalDevice pdev, const VkMemoryRequirements& mr, VkMemoryPropertyFlags need) {
    VkPhysicalDeviceMemoryProperties mpp;
    vkGetPhysicalDeviceMemoryProperties(pdev, &mpp);
    for (uint32_t i = 0; i < mpp.memoryTypeCount; ++i)
        if ((mr.memoryTypeBits & (1u << i)) && (mpp.memoryTypes[i].propertyFlags & need) == need)
            return i;
    return 0;
}

// Create a HOST_VISIBLE|HOST_COHERENT buffer (+ memory); optionally fill it with `data`.
// Everything the app uploads per frame is host-visible (small CPU->GPU copies).
inline void vkMakeBuffer(VkDevice dev, VkPhysicalDevice pdev, VkBuffer& buf, VkDeviceMemory& mem,
                         VkDeviceSize size, VkBufferUsageFlags usage, const void* data) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    VK(vkCreateBuffer(dev, &bci, nullptr, &buf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, buf, &mr);
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = vkFindMemoryType(pdev, mr,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkAllocateMemory(dev, &maa, nullptr, &mem));
    VK(vkBindBufferMemory(dev, buf, mem, 0));
    if (data) {
        void* p;
        VK(vkMapMemory(dev, mem, 0, size, 0, &p));
        std::memcpy(p, data, size);
        vkUnmapMemory(dev, mem);
    }
}
