#pragma once
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
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

inline void vkSubmitAndWait(VkDevice dev, VkPhysicalDevice pdev, const std::function<void(VkCommandBuffer)>& upload) {
    uint32_t fam = 0, famCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pdev, &famCount, nullptr);
    std::vector<VkQueueFamilyProperties> fams(famCount);
    vkGetPhysicalDeviceQueueFamilyProperties(pdev, &famCount, fams.data());
    for (uint32_t i = 0; i < famCount; ++i)
        if (fams[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            fam = i;
            break;
        }
    VkQueue q;
    vkGetDeviceQueue(dev, fam, 0, &q);
    VkCommandPool pool;
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = fam;
    VK(vkCreateCommandPool(dev, &cpci, nullptr, &pool));
    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo cba{};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = pool;
    cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = 1;
    VK(vkAllocateCommandBuffers(dev, &cba, &cb));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK(vkBeginCommandBuffer(cb, &cbi));
    upload(cb);
    VK(vkEndCommandBuffer(cb));
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(q));
    vkFreeCommandBuffers(dev, pool, 1, &cb);
    vkDestroyCommandPool(dev, pool, nullptr);
}

inline void vkImgBarrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlagBits aspect, VkImageLayout oldL,
                        VkImageLayout newL, VkAccessFlags srcAcc, VkAccessFlags dstAcc, uint32_t mips,
                        VkPipelineStageFlagBits srcStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                        VkPipelineStageFlagBits dstStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                        uint32_t baseLevel = 0, uint32_t layers = 1) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.image = img;
    b.subresourceRange = {aspect, baseLevel, mips, 0, layers};
    b.oldLayout = oldL;
    b.newLayout = newL;
    b.srcAccessMask = srcAcc;
    b.dstAccessMask = dstAcc;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
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

inline void vkMakeBufferDev(VkDevice dev, VkPhysicalDevice pdev, VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize size,
                            VkBufferUsageFlags usage, const void* data) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage | (data ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
    VK(vkCreateBuffer(dev, &bci, nullptr, &buf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, buf, &mr);
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = vkFindMemoryType(pdev, mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(dev, &maa, nullptr, &mem));
    VK(vkBindBufferMemory(dev, buf, mem, 0));
    if (!data)
        return;
    VkBuffer stage;
    VkDeviceMemory stageMem;
    VkBufferCreateInfo sbi{};
    sbi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbi.size = size;
    sbi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK(vkCreateBuffer(dev, &sbi, nullptr, &stage));
    vkGetBufferMemoryRequirements(dev, stage, &mr);
    VkMemoryAllocateInfo smaa{};
    smaa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    smaa.allocationSize = mr.size;
    smaa.memoryTypeIndex =
        vkFindMemoryType(pdev, mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkAllocateMemory(dev, &smaa, nullptr, &stageMem));
    VK(vkBindBufferMemory(dev, stage, stageMem, 0));
    void* p;
    VK(vkMapMemory(dev, stageMem, 0, size, 0, &p));
    std::memcpy(p, data, size);
    vkUnmapMemory(dev, stageMem);
    const VkBufferCopy r{0, 0, size};
    vkSubmitAndWait(dev, pdev, [&](VkCommandBuffer cb) { vkCmdCopyBuffer(cb, stage, buf, 1, &r); });
    vkDestroyBuffer(dev, stage, nullptr);
    vkFreeMemory(dev, stageMem, nullptr);
}

inline void vkWriteBuffer(VkDevice dev, VkDeviceMemory mem, const void* data, VkDeviceSize size) {
    void* p;
    VK(vkMapMemory(dev, mem, 0, size, 0, &p));
    std::memcpy(p, data, size);
    vkUnmapMemory(dev, mem);
}

inline void vkFreeBuffer(VkDevice dev, VkBuffer buf, VkDeviceMemory mem) {
    if (buf) {
        vkDestroyBuffer(dev, buf, nullptr);
        vkFreeMemory(dev, mem, nullptr);
    }
}

// run one upload command on a transient queue and wait for it
inline VkShaderModule vkMakeModule(VkDevice dev, const void* code, uint32_t nwords) {
    VkShaderModuleCreateInfo smc{};
    smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smc.codeSize = nwords * sizeof(uint32_t);
    smc.pCode = static_cast<const uint32_t*>(code);
    VkShaderModule m;
    VK(vkCreateShaderModule(dev, &smc, nullptr, &m));
    return m;
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
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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
    vkSubmitAndWait(dev, pdev, [&](VkCommandBuffer cb) {
        vkImgBarrier(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                     VK_ACCESS_SHADER_READ_BIT, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });
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

inline void vkMakeSamplerEx(VkDevice dev, VkPhysicalDevice pdev, VkSamplerAddressMode addrU, VkSamplerAddressMode addrV,
                            bool linearMip, float maxLod, float maxAniso, VkSampler& out) {
    VkPhysicalDeviceProperties pp{};
    vkGetPhysicalDeviceProperties(pdev, &pp);
    const float aniso = maxAniso > 1.0f ? std::min(maxAniso, pp.limits.maxSamplerAnisotropy) : 1.0f;
    VkSamplerCreateInfo s{};
    s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter = VK_FILTER_LINEAR;
    s.minFilter = VK_FILTER_LINEAR;
    s.mipmapMode = linearMip ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s.addressModeU = addrU;
    s.addressModeV = addrV;
    s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.anisotropyEnable = aniso > 1.0f;
    s.maxAnisotropy = aniso;
    s.compareEnable = VK_FALSE;
    s.minLod = 0.0f;
    s.maxLod = maxLod;
    s.unnormalizedCoordinates = VK_FALSE;
    VK(vkCreateSampler(dev, &s, nullptr, &out));
}

inline void vkMakeCubeImage(VkDevice dev, VkPhysicalDevice pdev, uint32_t size, uint32_t mipLevels, VkImage& img,
                            VkDeviceMemory& mem, VkImageView& cubeView, VkFormat fmt = VK_FORMAT_R16G16B16A16_SFLOAT,
                            VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) {
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = {size, size, 1};
    ici.mipLevels = mipLevels;
    ici.arrayLayers = 6;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = tiling;
    ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; // cube + 2D-array views on one image
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK(vkCreateImage(dev, &ici, nullptr, &img));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = vkFindMemoryType(pdev, mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(dev, &maa, nullptr, &mem));
    VK(vkBindImageMemory(dev, img, mem, 0));
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    vci.format = fmt;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6};
    VK(vkCreateImageView(dev, &vci, nullptr, &cubeView));
}

inline void vkMakeDepthCubeImage(VkDevice dev, VkPhysicalDevice pdev, uint32_t size, VkImage& img,
                                 VkDeviceMemory& mem) {
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_D32_SFLOAT;
    ici.extent = {size, size, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 6;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK(vkCreateImage(dev, &ici, nullptr, &img));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = vkFindMemoryType(pdev, mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(dev, &maa, nullptr, &mem));
    VK(vkBindImageMemory(dev, img, mem, 0));
}

inline void vkMakeCubeArrayImage(VkDevice dev, VkPhysicalDevice pdev, uint32_t size, uint32_t arrayLayers, VkFormat fmt,
                                 VkImage& img, VkDeviceMemory& mem) {
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = {size, size, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = arrayLayers;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK(vkCreateImage(dev, &ici, nullptr, &img));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = vkFindMemoryType(pdev, mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(dev, &maa, nullptr, &mem));
    VK(vkBindImageMemory(dev, img, mem, 0));
}

inline void vkMakeCubeArrayView(VkDevice dev, VkImage img, VkFormat fmt, VkImageAspectFlags aspect, uint32_t baseLayer,
                                uint32_t layerCount, VkImageView& view) {
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    vci.format = fmt;
    vci.subresourceRange = {aspect, 0, 1, baseLayer, layerCount};
    VK(vkCreateImageView(dev, &vci, nullptr, &view));
}

inline void vkMakeCubeFaceView(VkDevice dev, VkImage img, VkFormat fmt, VkImageAspectFlags aspect, uint32_t face,
                               VkImageView& view) {
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange = {aspect, 0, 1, face, 1};
    VK(vkCreateImageView(dev, &vci, nullptr, &view));
}

inline void vkMakeCubeMipArrayView(VkDevice dev, VkImage img, uint32_t mip, VkImageView& view) {
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6};
    VK(vkCreateImageView(dev, &vci, nullptr, &view));
}

inline void vkMakeCubeMipView(VkDevice dev, VkImage img, uint32_t mip, VkImageView& view) {
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    vci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6};
    VK(vkCreateImageView(dev, &vci, nullptr, &view));
}

inline VkDescriptorPool vkMakePool(VkDevice dev, const std::vector<VkDescriptorSetLayoutBinding>& binds,
                                   uint32_t maxSets) {
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
    VkDescriptorPool pool;
    VK(vkCreateDescriptorPool(dev, &dpc, nullptr, &pool));
    return pool;
}

inline void vkMakeDslPool(VkDevice dev, const std::vector<VkDescriptorSetLayoutBinding>& binds, uint32_t maxSets,
                          VkDescriptorSetLayout& dsl, VkDescriptorPool& pool) {
    VkDescriptorSetLayoutCreateInfo dslc{};
    dslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslc.bindingCount = (uint32_t)binds.size();
    dslc.pBindings = binds.data();
    VK(vkCreateDescriptorSetLayout(dev, &dslc, nullptr, &dsl));
    pool = vkMakePool(dev, binds, maxSets);
}

struct WriteDescs {
    struct Slot {
        VkDescriptorSet set;
        uint32_t binding;
        VkDescriptorType type;
        int imgIdx = -1;
        int bufIdx = -1;
    };
    std::vector<Slot> slots;
    std::vector<VkDescriptorImageInfo> img;
    std::vector<VkDescriptorBufferInfo> buf;
    void image(VkDescriptorSet set, uint32_t binding, const VkDescriptorImageInfo& ii,
               VkDescriptorType t = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        img.push_back(ii);
        slots.push_back({set, binding, t, (int)img.size() - 1, -1});
    }
    void buffer(VkDescriptorSet set, uint32_t binding, const VkDescriptorBufferInfo& bi,
                VkDescriptorType t = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        buf.push_back(bi);
        slots.push_back({set, binding, t, -1, (int)buf.size() - 1});
    }
    void update(VkDevice dev) {
        if (slots.empty())
            return;
        std::vector<VkWriteDescriptorSet> w(slots.size());
        for (size_t i = 0; i < slots.size(); ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = slots[i].set;
            w[i].dstBinding = slots[i].binding;
            w[i].descriptorCount = 1;
            w[i].descriptorType = slots[i].type;
            if (slots[i].imgIdx >= 0)
                w[i].pImageInfo = &img[(size_t)slots[i].imgIdx];
            else
                w[i].pBufferInfo = &buf[(size_t)slots[i].bufIdx];
        }
        vkUpdateDescriptorSets(dev, (uint32_t)w.size(), w.data(), 0, nullptr);
    }
};

inline void vkMakeSet(VkDevice dev, VkDescriptorPool pool, VkDescriptorSetLayout dsl,
                      const std::vector<VkDescriptorSetLayoutBinding>& binds, VkDescriptorSet& set,
                      const std::vector<VkDescriptorBufferInfo>& bufs) {
    VkDescriptorSetAllocateInfo sa{};
    sa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sa.descriptorPool = pool;
    sa.descriptorSetCount = 1;
    sa.pSetLayouts = &dsl;
    VK(vkAllocateDescriptorSets(dev, &sa, &set));
    WriteDescs wd;
    for (size_t i = 0; i < bufs.size(); ++i)
        wd.buffer(set, binds[i].binding, bufs[i], binds[i].descriptorType);
    wd.update(dev);
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

inline VkPipelineLayout vkMakePipelineLayoutPC(VkDevice dev, VkDescriptorSetLayout dsl, VkShaderStageFlags stages,
                                               uint32_t size) {
    VkPushConstantRange range{stages, 0, size};
    VkPipelineLayoutCreateInfo plc{};
    plc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plc.setLayoutCount = 1;
    plc.pSetLayouts = &dsl;
    plc.pushConstantRangeCount = 1;
    plc.pPushConstantRanges = &range;
    VkPipelineLayout pl;
    VK(vkCreatePipelineLayout(dev, &plc, nullptr, &pl));
    return pl;
}

inline VkPipeline makeGraphicsPipeline(VkDevice dev, VkRenderPass rp, const void* vsSpv, uint32_t vsLen,
                                       const void* fsSpv, uint32_t fsLen, VkPipelineLayout layout, bool depth,
                                       bool cullBack = false, bool blend = false, uint32_t nColorAttach = 1) {
    VkShaderModule vs = vkMakeModule(dev, vsSpv, vsLen);
    VkShaderModule fs = vkMakeModule(dev, fsSpv, fsLen);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dyn;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = cullBack ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = depth ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depth && !blend ? VK_TRUE : VK_FALSE; // transparent: test but don't write
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.minDepthBounds = 0.0f;
    ds.maxDepthBounds = 1.0f;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState ca[2]{};
    for (uint32_t i = 0; i < nColorAttach; ++i) {
        auto& c = ca[i];
        c.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        c.blendEnable = blend ? VK_TRUE : VK_FALSE;
        if (blend) {
            c.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            c.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            c.colorBlendOp = VK_BLEND_OP_ADD;
            c.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            c.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            c.alphaBlendOp = VK_BLEND_OP_ADD;
        }
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = nColorAttach;
    cb.pAttachments = ca;
    VkGraphicsPipelineCreateInfo gpc{};
    gpc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpc.stageCount = 2;
    gpc.pStages = stages;
    gpc.pVertexInputState = &vi;
    gpc.pInputAssemblyState = &ia;
    gpc.pDynamicState = &dynState;
    gpc.pViewportState = &vp;
    gpc.pRasterizationState = &rs;
    gpc.pMultisampleState = &ms;
    gpc.pDepthStencilState = &ds;
    gpc.pColorBlendState = &cb;
    gpc.layout = layout;
    gpc.renderPass = rp;
    VkPipeline pipe;
    VK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpc, nullptr, &pipe));
    vkDestroyShaderModule(dev, vs, nullptr);
    vkDestroyShaderModule(dev, fs, nullptr);
    return pipe;
}
