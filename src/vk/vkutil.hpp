#pragma once
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define VK(x)                                                                                                          \
    do {                                                                                                               \
        VkResult _r = (x);                                                                                             \
        if (_r != VK_SUCCESS) {                                                                                        \
            std::fprintf(stderr, "vk error %d @%s:%d\n", (int)_r, __FILE__, __LINE__);                                 \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)

inline uint32_t vkFindMemoryType(VkPhysicalDevice pdev, const VkMemoryRequirements& mr, VkMemoryPropertyFlags need) {
    VkPhysicalDeviceMemoryProperties mpp;
    vkGetPhysicalDeviceMemoryProperties(pdev, &mpp);
    for (uint32_t i = 0; i < mpp.memoryTypeCount; ++i)
        if ((mr.memoryTypeBits & (1u << i)) && (mpp.memoryTypes[i].propertyFlags & need) == need)
            return i;
    return 0;
}

inline void vkMakeBuffer(VkDevice dev, VkPhysicalDevice pdev, VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize size,
                         VkBufferUsageFlags usage, const void* data) {
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
    maa.memoryTypeIndex =
        vkFindMemoryType(pdev, mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkAllocateMemory(dev, &maa, nullptr, &mem));
    VK(vkBindBufferMemory(dev, buf, mem, 0));
    if (data) {
        void* p;
        VK(vkMapMemory(dev, mem, 0, size, 0, &p));
        std::memcpy(p, data, size);
        vkUnmapMemory(dev, mem);
    }
}

inline void vkWriteBuffer(VkDevice dev, VkDeviceMemory mem, const void* data, VkDeviceSize size) {
    void* p;
    VK(vkMapMemory(dev, mem, 0, size, 0, &p));
    std::memcpy(p, data, size);
    vkUnmapMemory(dev, mem);
}

inline void vkZeroBuffer(VkDevice dev, VkDeviceMemory mem, VkDeviceSize size) {
    void* p;
    VK(vkMapMemory(dev, mem, 0, size, 0, &p));
    std::memset(p, 0, size);
    vkUnmapMemory(dev, mem);
}

inline void vkFreeBuffer(VkDevice dev, VkBuffer buf, VkDeviceMemory mem) {
    if (buf) {
        vkDestroyBuffer(dev, buf, nullptr);
        vkFreeMemory(dev, mem, nullptr);
    }
}

inline VkShaderModule vkMakeModule(VkDevice dev, const void* code, uint32_t nwords) {
    VkShaderModuleCreateInfo smc{};
    smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smc.codeSize = nwords * sizeof(uint32_t);
    smc.pCode = static_cast<const uint32_t*>(code);
    VkShaderModule m;
    VK(vkCreateShaderModule(dev, &smc, nullptr, &m));
    return m;
}

inline void vkMakeSampler(VkDevice dev, VkSamplerAddressMode addr, VkSampler& out) {
    VkSamplerCreateInfo s{};
    s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter = VK_FILTER_LINEAR;
    s.minFilter = VK_FILTER_LINEAR;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s.addressModeU = addr;
    s.addressModeV = addr;
    s.addressModeW = addr;
    s.anisotropyEnable = VK_FALSE;
    s.compareEnable = VK_FALSE;
    s.unnormalizedCoordinates = VK_FALSE;
    VK(vkCreateSampler(dev, &s, nullptr, &out));
}

inline void vkMakeImage2D(VkDevice dev, VkPhysicalDevice pdev, uint32_t w, uint32_t h, const void* rgba8, VkImage& img,
                          VkDeviceMemory& mem, VkImageView& view) {
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_LINEAR;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    VK(vkCreateImage(dev, &ici, nullptr, &img));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex =
        vkFindMemoryType(pdev, mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkAllocateMemory(dev, &maa, nullptr, &mem));
    VK(vkBindImageMemory(dev, img, mem, 0));
    if (rgba8) {
        void* p;
        VK(vkMapMemory(dev, mem, 0, (VkDeviceSize)w * h * 4, 0, &p));
        std::memcpy(p, rgba8, (size_t)w * h * 4);
        vkUnmapMemory(dev, mem);
    }
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(dev, &vci, nullptr, &view));
}

inline void vkFreeImage2D(VkDevice dev, VkImage img, VkDeviceMemory mem, VkImageView view) {
    if (img) {
        vkDestroyImageView(dev, view, nullptr);
        vkDestroyImage(dev, img, nullptr);
        vkFreeMemory(dev, mem, nullptr);
    }
}

inline void vkMakeDslPool(VkDevice dev, const std::vector<VkDescriptorSetLayoutBinding>& binds, uint32_t maxSets,
                          VkDescriptorSetLayout& dsl, VkDescriptorPool& pool) {
    VkDescriptorSetLayoutCreateInfo dslc{};
    dslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslc.bindingCount = (uint32_t)binds.size();
    dslc.pBindings = binds.data();
    VK(vkCreateDescriptorSetLayout(dev, &dslc, nullptr, &dsl));
    std::vector<VkDescriptorPoolSize> sizes;
    for (const auto& b : binds) {
        auto it =
            std::ranges::find_if(sizes, [&](const VkDescriptorPoolSize& s) { return s.type == b.descriptorType; });
        if (it == sizes.end())
            sizes.push_back({b.descriptorType, maxSets});
        else
            it->descriptorCount += maxSets;
    }
    VkDescriptorPoolCreateInfo dpc{};
    dpc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpc.maxSets = maxSets;
    dpc.poolSizeCount = (uint32_t)sizes.size();
    dpc.pPoolSizes = sizes.data();
    VK(vkCreateDescriptorPool(dev, &dpc, nullptr, &pool));
}

inline void vkMakeSet(VkDevice dev, VkDescriptorPool pool, VkDescriptorSetLayout dsl,
                      const std::vector<VkDescriptorSetLayoutBinding>& binds, VkDescriptorSet& set,
                      const std::vector<VkDescriptorBufferInfo>& bufs) {
    VkDescriptorSetAllocateInfo sa{};
    sa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sa.descriptorPool = pool;
    sa.descriptorSetCount = 1;
    sa.pSetLayouts = &dsl;
    VK(vkAllocateDescriptorSets(dev, &sa, &set));
    std::vector<VkWriteDescriptorSet> w(bufs.size());
    for (size_t i = 0; i < bufs.size(); ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = binds[i].binding;
        w[i].descriptorCount = 1;
        w[i].descriptorType = binds[i].descriptorType;
        w[i].pBufferInfo = &bufs[i];
    }
    vkUpdateDescriptorSets(dev, (uint32_t)w.size(), w.data(), 0, nullptr);
}

inline VkPipelineLayout vkMakePipelineLayout(VkDevice dev, VkDescriptorSetLayout dsl) {
    VkPipelineLayoutCreateInfo plc{};
    plc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plc.setLayoutCount = 1;
    plc.pSetLayouts = &dsl;
    VkPipelineLayout pl;
    VK(vkCreatePipelineLayout(dev, &plc, nullptr, &pl));
    return pl;
}
