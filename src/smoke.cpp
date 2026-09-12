#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "smoke_spv.hpp"

#define VK(x)                                                                                                       \
    do {                                                                                                            \
        VkResult _r = (x);                                                                                          \
        if (_r != VK_SUCCESS) {                                                                                     \
            fprintf(stderr, "Vulkan error %d at %s:%d\n", _r, __FILE__, __LINE__);                                  \
            abort();                                                                                                \
        }                                                                                                           \
    } while (0)

// Mirror of the GLSL struct (vec3 a @0, vec3 b @16, float r @28, pad @32) => 48-byte stride.
struct Capsule {
    float a[3];
    alignas(16) float b[3];
    float r;
    float pad;
};
static_assert(sizeof(Capsule) == 48, "Capsule must be 48-byte stride");

struct Vec3 {
    float x, y, z;
    float _pad; // SSBO vec3 stride is 16 bytes
};

struct Params {
    uint32_t n_capsules;
    uint32_t n_points;
};

VkQueue g_queue;

struct Buffer {
    VkBuffer vk = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

Buffer make_buffer(VkDevice dev, VkPhysicalDevice pdev, VkDeviceSize size, VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags memflags) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    VK(vkCreateBuffer(dev, &bi, nullptr, &b.vk));

    VkMemoryRequirements m;
    vkGetBufferMemoryRequirements(dev, b.vk, &m);
    VkPhysicalDeviceMemoryProperties p;
    vkGetPhysicalDeviceMemoryProperties(pdev, &p);
    uint32_t mt = 0;
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i) {
        if ((m.memoryTypeBits & (1u << i)) && (memflags & p.memoryTypes[i].propertyFlags) == memflags) {
            mt = i;
            break;
        }
    }
    VkMemoryAllocateInfo a{};
    a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    a.allocationSize = m.size;
    a.memoryTypeIndex = mt;
    VK(vkAllocateMemory(dev, &a, nullptr, &b.mem));
    VK(vkBindBufferMemory(dev, b.vk, b.mem, 0));
    return b;
}

int main() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "3dsim";
    app.apiVersion = VK_MAKE_VERSION(1, 2, 0);
    VkInstanceCreateInfo ic{};
    ic.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ic.pApplicationInfo = &app;
    VkInstance inst;
    VK(vkCreateInstance(&ic, nullptr, &inst));

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(inst, &n, devs.data());

    VkPhysicalDevice pdev = VK_NULL_HANDLE;
    uint32_t qf = 0;
    bool have_discrete = false;
    for (auto d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        uint32_t qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qps(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, qps.data());
        uint32_t cqf = ~0u;
        for (uint32_t i = 0; i < qc; ++i)
            if (qps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                cqf = i;
                break;
            }
        if (cqf == ~0u)
            continue;
        bool discrete = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        if (pdev == VK_NULL_HANDLE || (!have_discrete && discrete)) {
            pdev = d;
            qf = cqf;
            have_discrete = discrete;
        }
    }
    if (!pdev) {
        fprintf(stderr, "no compute device\n");
        return 1;
    }
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(pdev, &pp);
    printf("device: %s\n", pp.deviceName);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qc{};
    qc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qc.queueFamilyIndex = qf;
    qc.queueCount = 1;
    qc.pQueuePriorities = &prio;
    VkDeviceCreateInfo dc{};
    dc.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dc.queueCreateInfoCount = 1;
    dc.pQueueCreateInfos = &qc;
    VkDevice dev;
    VK(vkCreateDevice(pdev, &dc, nullptr, &dev));
    vkGetDeviceQueue(dev, qf, 0, &g_queue);

    VkCommandPoolCreateInfo cpc{};
    cpc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpc.queueFamilyIndex = qf;
    VkCommandPool pool;
    VK(vkCreateCommandPool(dev, &cpc, nullptr, &pool));

    // --- one capsule along +X, radius 1 ---
    Capsule cap{};
    std::memcpy(cap.a, (const float[]){0, 0, 0}, sizeof cap.a);
    std::memcpy(cap.b, (const float[]){10, 0, 0}, sizeof cap.b);
    cap.r = 1.0f;
    const std::vector<Vec3> pts = {{5, 0, 0, 0}, {5, 1, 0, 0}, {5, 3, 0, 0}, {0, 0, 0, 0},
                                   {10, 0, 0, 0}, {-5, 0, 0, 0}, {15, 0, 0, 0}, {5, 0, 5, 0}};
    const std::vector<float> expected = {-1, 0, 2, -1, -1, 4, 4, 4};
    const uint32_t npts = (uint32_t)pts.size();

    Buffer bcap = make_buffer(dev, pdev, sizeof(Capsule), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    Buffer bpts = make_buffer(dev, pdev, sizeof(Vec3) * npts, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    Buffer bres = make_buffer(dev, pdev, sizeof(float) * npts, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buffer bpar = make_buffer(dev, pdev, sizeof(Params), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* p;
    VK(vkMapMemory(dev, bcap.mem, 0, bcap.size, 0, &p));
    std::memcpy(p, &cap, sizeof cap);
    vkUnmapMemory(dev, bcap.mem);
    VK(vkMapMemory(dev, bpts.mem, 0, bpts.size, 0, &p));
    std::memcpy(p, pts.data(), sizeof(Vec3) * npts);
    vkUnmapMemory(dev, bpts.mem);
    Params par{1, npts};
    VK(vkMapMemory(dev, bpar.mem, 0, bpar.size, 0, &p));
    std::memcpy(p, &par, sizeof par);
    vkUnmapMemory(dev, bpar.mem);

    // descriptor set: 0=capsules,1=points,2=result(storage)  3=params(uniform)
    VkDescriptorSetLayoutBinding bl[4]{};
    for (uint32_t i = 0; i < 3; ++i) {
        bl[i].binding = i;
        bl[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bl[i].descriptorCount = 1;
        bl[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bl[3].binding = 3;
    bl[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bl[3].descriptorCount = 1;
    bl[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 4;
    dsl.pBindings = bl;
    VkDescriptorSetLayout layout;
    VK(vkCreateDescriptorSetLayout(dev, &dsl, nullptr, &layout));

    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dp{};
    dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.maxSets = 1;
    dp.poolSizeCount = 2;
    dp.pPoolSizes = ps;
    VkDescriptorPool dpool;
    VK(vkCreateDescriptorPool(dev, &dp, nullptr, &dpool));

    VkDescriptorSet set;
    VkDescriptorSetAllocateInfo sa{};
    sa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sa.descriptorPool = dpool;
    sa.descriptorSetCount = 1;
    sa.pSetLayouts = &layout;
    VK(vkAllocateDescriptorSets(dev, &sa, &set));

    VkDescriptorBufferInfo bi[4]{};
    bi[0].buffer = bcap.vk;
    bi[0].range = bcap.size;
    bi[1].buffer = bpts.vk;
    bi[1].range = bpts.size;
    bi[2].buffer = bres.vk;
    bi[2].range = bres.size;
    bi[3].buffer = bpar.vk;
    bi[3].range = bpar.size;
    VkWriteDescriptorSet w[4]{};
    for (int i = 0; i < 4; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = (i < 3) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[i].pBufferInfo = &bi[i];
    }
    vkUpdateDescriptorSets(dev, 4, w, 0, nullptr);

    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &layout;
    VkPipelineLayout playout;
    VK(vkCreatePipelineLayout(dev, &pl, nullptr, &playout));

    VkShaderModuleCreateInfo smc{};
    smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smc.codeSize = smoke_spv_len;
    smc.pCode = (const uint32_t*)smoke_spv;
    VkShaderModule mod;
    VK(vkCreateShaderModule(dev, &smc, nullptr, &mod));
    VkComputePipelineCreateInfo pc{};
    pc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pc.layout = playout;
    pc.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pc.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.stage.module = mod;
    pc.stage.pName = "main";
    VkPipeline pipe;
    VK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pc, nullptr, &pipe));

    VkCommandBuffer cmdb;
    VkCommandBufferAllocateInfo ca{};
    ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ca.commandPool = pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VK(vkAllocateCommandBuffers(dev, &ca, &cmdb));
    VkCommandBufferBeginInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK(vkBeginCommandBuffer(cmdb, &cb));
    vkCmdBindPipeline(cmdb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cmdb, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &set, 0, nullptr);
    vkCmdDispatch(cmdb, (npts + 63) / 64, 1, 1);
    VK(vkEndCommandBuffer(cmdb));

    VkFence fence;
    VkFenceCreateInfo fc{};
    fc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK(vkCreateFence(dev, &fc, nullptr, &fence));
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmdb;
    VK(vkQueueSubmit(g_queue, 1, &si, fence));
    VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 1000000000ull));

    VK(vkMapMemory(dev, bres.mem, 0, bres.size, 0, &p));
    const float* out = (const float*)p;
    printf("point          got        exp\n");
    bool ok = true;
    for (uint32_t i = 0; i < npts; ++i) {
        bool good = (std::abs(out[i] - expected[i]) < 1e-3f);
        ok = ok && good;
        printf("(% 4.0f,% 4.0f,% 4.0f)  %+6.2f   %+6.2f  %s\n", pts[i].x, pts[i].y, pts[i].z, out[i], expected[i],
               good ? "ok" : "FAIL");
    }
    vkUnmapMemory(dev, bres.mem);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
