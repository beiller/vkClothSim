// 3dsim – pure Vulkan + GLFW + ImGui (Vulkan backend).
// Phase 1: a visible window. Vulkan renders a full-screen background triangle whose
// color is driven by an ImGui slider (proves "UI controls the Vulkan renderer").
// `--shot out.ppm` renders one frame, reads it back, writes a PPM, and exits.
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>

#include "vk_bg_vert_spv.hpp"
#include "vk_bg_frag_spv.hpp"
#include "caps_vert_spv.hpp"
#include "caps_frag_spv.hpp"
#include "cloth_spv.hpp"
#include "cloth_vert_spv.hpp"
#include "cloth_frag_spv.hpp"
#include "ball_vert_spv.hpp"
#include "ball_frag_spv.hpp"

#include "sim/sim.hpp"     // universal soft-body solver (vertex list + distance constraints)
#include "app/rigid.hpp"   // rigid-body physics (Jolt: ground + 16 clumping capsules)
#include "app/ui.hpp"      // the Dear ImGui overlay (HUD + controls)

#define VK(x)                                                                                                       \
    do {                                                                                                            \
        VkResult _r = (x);                                                                                          \
        if (_r != VK_SUCCESS) {                                                                                     \
            std::fprintf(stderr, "vk error %d @%s:%d\n", (int)_r, __FILE__, __LINE__);                              \
            std::abort();                                                                                           \
        }                                                                                                           \
    } while (0)

namespace {
VkInstance g_inst;
VkSurfaceKHR g_surface;
VkPhysicalDevice g_pdev;
VkDevice g_dev;
VkQueue g_queue;
uint32_t g_qf;
VkSwapchainKHR g_sc;
std::vector<VkImage> g_images;
std::vector<VkImageView> g_views;
VkRenderPass g_rp;
std::vector<VkFramebuffer> g_fbs;
VkCommandPool g_pool;
VkCommandBuffer g_cmd;
VkFence g_fence;

// scene (full-screen background triangle)
VkDescriptorSetLayout g_dsl;
VkDescriptorPool g_dpool;
VkDescriptorSet g_dset;
VkBuffer g_ubuf;
VkDeviceMemory g_umem;
VkBuffer g_vbuf;
VkDeviceMemory g_vmem;
VkPipelineLayout g_pl;
VkPipeline g_pipe;
VkExtent2D g_extent;
VkFormat g_scfmt;

// --- user-tunable parameters (the UI) ---
static UIState g_ui;

// --- minimal column-major mat4 / vec3 ---
struct Mat4 { float m[16]; };
struct V3 { float x, y, z; };
static float vDot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vSub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 vCross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static V3 vNorm(V3 a) { float l = std::sqrt(vDot(a, a)); return {a.x / l, a.y / l, a.z / l}; }
static Mat4 mul4(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int rw = 0; rw < 4; ++rw) {
            float s = 0;
            for (int k = 0; k < 4; ++k)
                s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}
static Mat4 perspective(float fovyDeg, float aspect, float nearP, float farP) {
    Mat4 r{};
    float f = 1.0f / std::tan(fovyDeg * 0.5f * 3.14159265f / 180.0f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (farP + nearP) / (nearP - farP);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * farP * nearP / (nearP - farP);
    return r;
}
static Mat4 lookAt(V3 eye, V3 at, V3 up) {
    V3 f = vNorm(vSub(at, eye));
    V3 s = vNorm(vCross(f, up));
    V3 u = vCross(s, f);
    Mat4 r{};
    r.m[0] = s.x; r.m[1] = u.x; r.m[2] = -f.x; r.m[3] = 0;
    r.m[4] = s.y; r.m[5] = u.y; r.m[6] = -f.y; r.m[7] = 0;
    r.m[8] = s.z; r.m[9] = u.z; r.m[10] = -f.z; r.m[11] = 0;
    r.m[12] = -vDot(s, eye); r.m[13] = -vDot(u, eye); r.m[14] = vDot(f, eye); r.m[15] = 1;
    return r;
}
Mat4 g_vp;

// --- rigid-body physics (Jolt: ground + 16 clumping capsules) ---
static RigidScene g_rigid;

// --- depth ---
VkImage g_depth;
VkDeviceMemory g_depthMem;
VkImageView g_depthView;

// --- capsule render ---
VkBuffer g_cvbuf, g_cibuf;
VkDeviceMemory g_cvbmem, g_cibmem;
VkBuffer g_cubuf;
VkDeviceMemory g_cubmem;
VkBuffer g_csbuf;
VkDeviceMemory g_csbufmem;
VkDescriptorSetLayout g_cdsl;
VkDescriptorPool g_cdpool;
VkDescriptorSet g_cdset;
VkPipelineLayout g_cpl;
VkPipeline g_cpipe;
uint32_t g_cidxCount = 0;

// --- cloth (governed by the universal solver; one-way capsule/ground collision in the app) ---
constexpr int CW = 64, CH = 64;
constexpr int CN = CW * CH;
constexpr int CLOTH_SUBSTEPS = 3;   // Verlet sub-steps per physics step (stability)
constexpr int CLOTH_HOLD = 150;     // frames to let the capsules clump before the cloth drops
const float CLOTH_Y0 = 10.0f, CLOTH_SPAN = 8.0f;
struct ClothParams { int nCaps; float gravity[3]; float dt; int W; int H; int iterations; int pinned; int substeps; };
static_assert(sizeof(ClothParams) == 40);
VkBuffer g_clothBuf, g_clothIbo, g_clothPBuf;
VkDeviceMemory g_clothBmem, g_clothIboMem, g_clothPMem;
sim::SoftBody g_clothBody;          // universal solver (vertex list + distance constraints)
std::vector<float> g_clothInit;     // initial vertex list (3 floats/vert), for reset
std::vector<float> g_clothBaseRest; // base rest length per constraint (tension = 1.0)
int g_clothPinned = 1, g_steps = 0, g_clothSubsteps = CLOTH_SUBSTEPS;
VkDescriptorSetLayout g_clDslC, g_clDslR;
VkDescriptorPool g_clPoolC, g_clPoolR;
VkDescriptorSet g_clSetC, g_clSetR;
VkPipelineLayout g_clPlC, g_clPlR;
VkPipeline g_clPipeC, g_clPipeR;
uint32_t g_clothIdxCount = 0;

// --- ball (soft-body sphere; governed by the SAME universal solver as the cloth) ---
// It falls (gravity) and lands on the ground (one-way collision); the pressure (PBD
// volume) expands/deflates it. The triangles are app-level (pressure + render only).
constexpr int BLAT = 16, BLON = 16;              // sphere lat/lon divisions
constexpr int BN = 2 + (BLAT - 1) * BLON;        // pole + interior rings + pole
const float BALL_R = 1.5f, BALL_Y0 = 7.0f;       // radius + starting height (in camera view)
sim::SoftBody g_ballBody;                        // universal solver (same as the cloth)
std::vector<float> g_ballInit;                   // initial vertex list (3 floats/vert), for reset
std::vector<sim::Triangle> g_ballTris;           // triangles (pressure + render; NOT in the solver)
VkBuffer g_ballBuf, g_ballIbo, g_blBuf;
VkDeviceMemory g_ballBmem, g_ballIboMem, g_blUmem;
uint32_t g_ballIdxCount = 0;
VkDescriptorSetLayout g_blDslR;
VkDescriptorPool g_blPoolR;
VkDescriptorSet g_blSetR;
VkPipelineLayout g_blPlR;
VkPipeline g_blPipeR;
} // namespace

void createScenePipeline(VkFormat fmt) {
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslc{};
    dslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslc.bindingCount = 1;
    dslc.pBindings = &b;
    VK(vkCreateDescriptorSetLayout(g_dev, &dslc, nullptr, &g_dsl));

    VkBufferCreateInfo ubc{};
    ubc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ubc.size = 16;
    ubc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VK(vkCreateBuffer(g_dev, &ubc, nullptr, &g_ubuf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g_dev, g_ubuf, &mr);
    VkPhysicalDeviceMemoryProperties mpp;
    vkGetPhysicalDeviceMemoryProperties(g_pdev, &mpp);
    uint32_t mt = 0;
    for (uint32_t i = 0; i < mpp.memoryTypeCount; ++i)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mpp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
            { mt = i; break; }
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = mt;
    VK(vkAllocateMemory(g_dev, &maa, nullptr, &g_umem));
    VK(vkBindBufferMemory(g_dev, g_ubuf, g_umem, 0));

    // vertex buffer: full-screen triangle (explicit vertices, robust on all drivers)
    float verts[6] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
    VkBufferCreateInfo vbc{};
    vbc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vbc.size = sizeof(verts);
    vbc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VK(vkCreateBuffer(g_dev, &vbc, nullptr, &g_vbuf));
    vkGetBufferMemoryRequirements(g_dev, g_vbuf, &mr);
    uint32_t vmt = 0;
    for (uint32_t i = 0; i < mpp.memoryTypeCount; ++i)
        if ((mr.memoryTypeBits & (1u << i)) && (mpp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            { vmt = i; break; }
    VkMemoryAllocateInfo vmaa{};
    vmaa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vmaa.allocationSize = mr.size;
    vmaa.memoryTypeIndex = vmt;
    VK(vkAllocateMemory(g_dev, &vmaa, nullptr, &g_vmem));
    VK(vkBindBufferMemory(g_dev, g_vbuf, g_vmem, 0));
    void* vptr;
    VK(vkMapMemory(g_dev, g_vmem, 0, sizeof(verts), 0, &vptr));
    std::memcpy(vptr, verts, sizeof(verts));
    vkUnmapMemory(g_dev, g_vmem);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpc{};
    dpc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpc.maxSets = 1;
    dpc.poolSizeCount = 1;
    dpc.pPoolSizes = &ps;
    VK(vkCreateDescriptorPool(g_dev, &dpc, nullptr, &g_dpool));
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = g_dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g_dsl;
    VK(vkAllocateDescriptorSets(g_dev, &dsai, &g_dset));
    VkDescriptorBufferInfo bi{g_ubuf, 0, 16};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = g_dset;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(g_dev, 1, &w, 0, nullptr);

    VkPipelineLayoutCreateInfo plc{};
    plc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plc.setLayoutCount = 1;
    plc.pSetLayouts = &g_dsl;
    VK(vkCreatePipelineLayout(g_dev, &plc, nullptr, &g_pl));

    auto makeModule = [](const uint32_t* code, size_t n) {
        VkShaderModuleCreateInfo smc{};
        smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smc.codeSize = n * sizeof(uint32_t);
        smc.pCode = code;
        VkShaderModule m;
        VK(vkCreateShaderModule(g_dev, &smc, nullptr, &m));
        return m;
    };
    VkShaderModule vs = makeModule(reinterpret_cast<const uint32_t*>(vk_bg_vert_spv), vk_bg_vert_spv_len / 4);
    VkShaderModule fs = makeModule(reinterpret_cast<const uint32_t*>(vk_bg_frag_spv), vk_bg_frag_spv_len / 4);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bind{0, 8, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr{0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &attr;
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
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState ca{};
    ca.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ca;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    gpc.layout = g_pl;
    gpc.renderPass = g_rp;
    VK(vkCreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &gpc, nullptr, &g_pipe));
    vkDestroyShaderModule(g_dev, vs, nullptr);
    vkDestroyShaderModule(g_dev, fs, nullptr);
}

static uint32_t memTypeHostVisible(const VkMemoryRequirements& mr) {
    VkPhysicalDeviceMemoryProperties mpp;
    vkGetPhysicalDeviceMemoryProperties(g_pdev, &mpp);
    const VkMemoryPropertyFlags need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < mpp.memoryTypeCount; ++i)
        if ((mr.memoryTypeBits & (1u << i)) && (mpp.memoryTypes[i].propertyFlags & need) == need)
            return i;
    return 0;
}
static void makeBuffer(VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize size, VkBufferUsageFlags usage, const void* data) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    VK(vkCreateBuffer(g_dev, &bci, nullptr, &buf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g_dev, buf, &mr);
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = memTypeHostVisible(mr);
    VK(vkAllocateMemory(g_dev, &maa, nullptr, &mem));
    VK(vkBindBufferMemory(g_dev, buf, mem, 0));
    if (data) {
        void* p;
        VK(vkMapMemory(g_dev, mem, 0, size, 0, &p));
        std::memcpy(p, data, size);
        vkUnmapMemory(g_dev, mem);
    }
}

// Create a graphics pipeline with the shared scene state: triangle list, no culling,
// depth test + write, no blend, dynamic viewport/scissor. Takes the vertex + fragment
// shader modules (the caller destroys them after) + the pipeline layout.
static VkPipeline makeGraphicsPipeline(VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout) {
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
    vi.vertexBindingDescriptionCount = 0;
    vi.vertexAttributeDescriptionCount = 0;
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
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;
    ds.minDepthBounds = 0.0f;
    ds.maxDepthBounds = 1.0f;
    VkPipelineColorBlendAttachmentState ca{};
    ca.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    ca.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ca;
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
    gpc.renderPass = g_rp;
    VkPipeline pipe;
    VK(vkCreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &gpc, nullptr, &pipe));
    return pipe;
}

// Build all capsules' geometry (cylinder + 2 hemispheres) into one VBO/IBO.
// Each vertex: pos(3) nrm(3) capsuleIndex(1) = 7 floats (28 bytes).
static void buildCapsuleGeometry(std::vector<float>& verts, std::vector<uint32_t>& idx) {
    const int S = 20, M = 32;
    const float twoPi = 6.2831853f;
    const float R = 0.5f, H = 0.9f;   // all capsules share these dimensions
    for (size_t ci = 0; ci < (size_t)g_rigid.capsuleCount(); ++ci) {
        float top = H + R, bot = -H - R;
        uint32_t capBase = (uint32_t)(verts.size() / 7);
        for (int i = 0; i <= M; ++i) {
            float t = (float)i / M;
            float y = top - t * (top - bot);
            float r, dr;
            float dy = (y >= H) ? (y - H) : ((y <= -H) ? (y + H) : 0.0f);
            if (dy == 0.0f) {
                r = R;
                dr = 0.0f;
            } else {
                r = std::sqrt(std::max(0.0f, R * R - dy * dy));
                dr = (r > 1e-5f) ? -dy / r : 0.0f;
            }
            for (int j = 0; j < S; ++j) {
                float phi = (float)j / S * twoPi;
                float cp = std::cos(phi), sp = std::sin(phi);
                verts.push_back(r * cp);
                verts.push_back(y);
                verts.push_back(r * sp);
                verts.push_back(cp);
                verts.push_back(-dr);
                verts.push_back(sp);
                verts.push_back((float)ci);
            }
        }
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < S; ++j) {
                uint32_t jn = (j + 1) % S;
                uint32_t a = capBase + (uint32_t)i * S + j;
                uint32_t b = capBase + (uint32_t)i * S + jn;
                uint32_t c = capBase + (uint32_t)(i + 1) * S + j;
                uint32_t d = capBase + (uint32_t)(i + 1) * S + jn;
                idx.push_back(a); idx.push_back(b); idx.push_back(d);
                idx.push_back(a); idx.push_back(d); idx.push_back(c);
            }
    }
    // ground quad (world-space, flagged aIdx = -1)
    uint32_t gbase = (uint32_t)(verts.size() / 7);
    float G = 55.0f;
    const float gv[4][3] = {{-G, 0.0f, -G}, {G, 0.0f, -G}, {G, 0.0f, G}, {-G, 0.0f, G}};
    for (auto& v : gv) {
        verts.push_back(v[0]);
        verts.push_back(v[1]);
        verts.push_back(v[2]);
        verts.push_back(0.0f);
        verts.push_back(1.0f);
        verts.push_back(0.0f);
        verts.push_back(-1.0f);
    }
    idx.push_back(gbase); idx.push_back(gbase + 1); idx.push_back(gbase + 2);
    idx.push_back(gbase); idx.push_back(gbase + 2); idx.push_back(gbase + 3);
}

// Create the shared depth image + view (D32).
static void createDepth(VkExtent2D ext) {
    VkImageCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    dci.imageType = VK_IMAGE_TYPE_2D;
    dci.extent = {ext.width, ext.height, 1};
    dci.mipLevels = 1;
    dci.arrayLayers = 1;
    dci.format = VK_FORMAT_D32_SFLOAT;
    dci.tiling = VK_IMAGE_TILING_OPTIMAL;
    dci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    dci.samples = VK_SAMPLE_COUNT_1_BIT;
    dci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK(vkCreateImage(g_dev, &dci, nullptr, &g_depth));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(g_dev, g_depth, &mr);
    VkPhysicalDeviceMemoryProperties mpp;
    vkGetPhysicalDeviceMemoryProperties(g_pdev, &mpp);
    uint32_t mt = 0;
    for (uint32_t i = 0; i < mpp.memoryTypeCount; ++i)
        if ((mr.memoryTypeBits & (1u << i)) && (mpp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            { mt = i; break; }
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = mt;
    VK(vkAllocateMemory(g_dev, &maa, nullptr, &g_depthMem));
    VK(vkBindImageMemory(g_dev, g_depth, g_depthMem, 0));
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = g_depth;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_D32_SFLOAT;
    vci.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(g_dev, &vci, nullptr, &g_depthView));
}

// Step the rigid-body physics (the capsules) + count the physics steps (for the cloth's
// unpin logic). The RigidScene steps the Jolt system; the app tracks the step count.
static void stepRigid(int n) {
    g_rigid.step(n);
    g_steps += n;
}

// Cloth physics: step the universal solver (Verlet + sequential Gauss-Seidel), then apply
// the one-way collision. The solver is generic; gravity/damping/stiffness/tension are the
// app's UI parameters, pushed into the solver each step. Sequential Gauss-Seidel (no
// cross-thread race) => deterministic + stable.
static void stepCloth() {
    if (g_clothPinned)
        return;
    g_clothBody.gravity[1] = -9.81f * g_ui.cloth.mass;
    g_clothBody.damping = g_ui.cloth.damping;
    g_clothBody.iterations = g_ui.cloth.stiffness;
    g_clothBody.substeps = g_clothSubsteps;
    for (size_t i = 0; i < g_clothBody.cons.size(); ++i)   // tension scales the rest lengths
        g_clothBody.cons[i].rest = g_clothBaseRest[i] * g_ui.cloth.tension;
    g_clothBody.step();
    sim::applyCollision(g_clothBody, g_rigid.colliders());
}

// Ball physics: step the SAME universal solver as the cloth (Verlet + Gauss-Seidel), but
// WITH gravity so the ball FALLS, then apply the pressure (PBD volume) + the one-way
// collision (ground) so it lands and stays roughly round.
static void stepBall() {
    g_ballBody.gravity[1] = -9.81f;   // the ball falls (the cloth scales gravity by mass)
    const float vRest = 4.0f / 3.0f * 3.14159265f * BALL_R * BALL_R * BALL_R;
    const float vTarget = vRest * g_ui.ballPressure;
    g_ballBody.step();                 // CLOTH_SUBSTEPS sub-steps (set at init)
    sim::applyPressure(g_ballBody, g_ballTris, vTarget);
    sim::applyCollision(g_ballBody, g_rigid.colliders());
}

// Pack a soft body's 3-float vertices into the GPU's 16-byte (vec3) stride + upload the
// pos part to `buf`. The render reads only the pos part; the solver's `prev` is CPU-only.
// This is the bridge between the universal solver (12-byte verts) and the Vulkan buffers.
static void uploadVerts(const sim::SoftBody& body, VkBuffer buf, VkDeviceMemory mem) {
    const int n = body.numVertices();
    std::vector<float> gpu(4 * n);
    for (int i = 0; i < n; ++i) {
        const float* p = body.posPtr(i);
        gpu[4 * i + 0] = p[0]; gpu[4 * i + 1] = p[1]; gpu[4 * i + 2] = p[2]; gpu[4 * i + 3] = 0.0f;
    }
    void* cp;
    VK(vkMapMemory(g_dev, mem, 0, (VkDeviceSize)16 * n, 0, &cp));
    std::memcpy(cp, gpu.data(), (size_t)16 * n);
    vkUnmapMemory(g_dev, mem);
}

// Upload the (static) capsule transforms + viewProj + cloth params (pinned flag).
static void uploadClothParams();
// Upload the capsule transforms (from the RigidScene) to the host-visible SSBO + the
// (static) viewProj + cloth params.
static void uploadCapsules() {
    std::vector<CapsuleGPU> data = g_rigid.capsuleGPU();
    void* p;
    VK(vkMapMemory(g_dev, g_csbufmem, 0, 48 * data.size(), 0, &p));
    std::memcpy(p, data.data(), 48 * data.size());
    vkUnmapMemory(g_dev, g_csbufmem);
    void* up;
    VK(vkMapMemory(g_dev, g_cubmem, 0, 80, 0, &up));
    std::memcpy(up, g_vp.m, 64);
    std::memcpy((char*)up + 64, &CW, 4);
    std::memcpy((char*)up + 68, &CH, 4);
    vkUnmapMemory(g_dev, g_cubmem);
    uploadClothParams();
}

static void uploadClothParams() {
    ClothParams cp{};
    cp.nCaps = g_rigid.capsuleCount();
    cp.gravity[0] = 0.0f; cp.gravity[1] = -9.81f; cp.gravity[2] = 0.0f;
    cp.dt = (1.0f / 60.0f) / CLOTH_SUBSTEPS;
    cp.W = CW; cp.H = CH; cp.iterations = 6; cp.pinned = g_clothPinned; cp.substeps = g_clothSubsteps;
    void* up;
    VK(vkMapMemory(g_dev, g_clothPMem, 0, 40, 0, &up));
    std::memcpy(up, &cp, sizeof cp);
    vkUnmapMemory(g_dev, g_clothPMem);
}

// Re-pin the cloth back to its initial flat, held state (and reset the ball to its start).
static void resetCloth() {
    g_steps = 0;
    g_clothPinned = 1;
    g_clothBody.reset(g_clothInit.data());
    g_ballBody.reset(g_ballInit.data());
}

static void createCapsulePipeline() {
    std::vector<float> verts;
    std::vector<uint32_t> idx;
    buildCapsuleGeometry(verts, idx);
    g_cidxCount = (uint32_t)idx.size();
    makeBuffer(g_cvbuf, g_cvbmem, (VkDeviceSize)verts.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, verts.data());
    makeBuffer(g_cibuf, g_cibmem, (VkDeviceSize)idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, idx.data());
    makeBuffer(g_cubuf, g_cubmem, 80, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    makeBuffer(g_csbuf, g_csbufmem, 48 * (VkDeviceSize)g_rigid.capsuleCount(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);

    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo dslc{};
    dslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslc.bindingCount = 2;
    dslc.pBindings = binds;
    VK(vkCreateDescriptorSetLayout(g_dev, &dslc, nullptr, &g_cdsl));

    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dpc{};
    dpc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpc.maxSets = 1;
    dpc.poolSizeCount = 2;
    dpc.pPoolSizes = ps;
    VK(vkCreateDescriptorPool(g_dev, &dpc, nullptr, &g_cdpool));
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = g_cdpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g_cdsl;
    VK(vkAllocateDescriptorSets(g_dev, &dsai, &g_cdset));
    VkDescriptorBufferInfo bi[2] = {{g_cubuf, 0, 64}, {g_csbuf, 0, 48 * (VkDeviceSize)g_rigid.capsuleCount()}};
    VkWriteDescriptorSet w[2]{};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = g_cdset;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &bi[0];
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = g_cdset;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &bi[1];
    vkUpdateDescriptorSets(g_dev, 2, w, 0, nullptr);

    VkPipelineLayoutCreateInfo plc{};
    plc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plc.setLayoutCount = 1;
    plc.pSetLayouts = &g_cdsl;
    VK(vkCreatePipelineLayout(g_dev, &plc, nullptr, &g_cpl));

    auto makeModule = [](const uint32_t* code, size_t n) {
        VkShaderModuleCreateInfo smc{};
        smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smc.codeSize = n * sizeof(uint32_t);
        smc.pCode = code;
        VkShaderModule m;
        VK(vkCreateShaderModule(g_dev, &smc, nullptr, &m));
        return m;
    };
    VkShaderModule vs = makeModule(reinterpret_cast<const uint32_t*>(caps_vert_spv), caps_vert_spv_len / 4);
    VkShaderModule fs = makeModule(reinterpret_cast<const uint32_t*>(caps_frag_spv), caps_frag_spv_len / 4);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bind{0, 28, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32_SFLOAT, 24},
    };
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;
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
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;
    ds.minDepthBounds = 0.0f;
    ds.maxDepthBounds = 1.0f;
    VkPipelineColorBlendAttachmentState ca{};
    ca.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    ca.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ca;

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
    gpc.layout = g_cpl;
    gpc.renderPass = g_rp;
    VK(vkCreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &gpc, nullptr, &g_cpipe));
    vkDestroyShaderModule(g_dev, vs, nullptr);
    vkDestroyShaderModule(g_dev, fs, nullptr);
}

static void drawCapsules(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_cpipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_cpl, 0, 1, &g_cdset, 0, nullptr);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &g_cvbuf, &off);
    vkCmdBindIndexBuffer(cmd, g_cibuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, g_cidxCount, 1, 0, 0, 0);
}

// Build the cloth: a CW x CH grid sheet held flat above the pile. Combined pos+prev
// SSBO (16 B/pt each), a grid-triangle index buffer, and a 36-byte params buffer.
static void createCloth() {
    // Build the cloth's vertices + distance constraints (sim::makeCloth). The tension
    // (a UI param) scales the rest lengths each step; the solver stays generic.
    std::vector<float> verts, baseRest;
    std::vector<sim::Constraint> cons;
    sim::makeCloth(verts, cons, baseRest, CW, CH, CLOTH_SPAN, CLOTH_Y0);
    g_clothBaseRest = baseRest;
    g_clothInit = verts;
    float grav[3] = {0, -9.81f, 0};
    g_clothBody.init(verts.data(), CN, std::move(cons), (1.0f / 60.0f) / CLOTH_SUBSTEPS, grav, CLOTH_SUBSTEPS, 4, 0.999f);
    // Index buffer (triangles) for the render (NOT used by the solver).
    std::vector<uint32_t> idx;
    for (int gy = 0; gy < CH - 1; ++gy)
        for (int gx = 0; gx < CW - 1; ++gx) {
            uint32_t a = (uint32_t)(gy * CW + gx);
            idx.push_back(a); idx.push_back(a + 1); idx.push_back(a + CW);
            idx.push_back(a + 1); idx.push_back(a + CW + 1); idx.push_back(a + CW);
        }
    g_clothIdxCount = (uint32_t)idx.size();
    makeBuffer(g_clothBuf, g_clothBmem, (VkDeviceSize)16 * CN * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    uploadVerts(g_clothBody, g_clothBuf, g_clothBmem);   // initial (held) state into the GPU
    makeBuffer(g_clothIbo, g_clothIboMem, (VkDeviceSize)idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, idx.data());
    makeBuffer(g_clothPBuf, g_clothPMem, 40, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    uploadClothParams(); // set + upload initial pinned params (pinned=1)

    // compute descriptor set layout: 0 pos, 1 prev, 2 capsules, 3 params
    VkDescriptorSetLayoutBinding cb[4]{};
    cb[0].binding = 0; cb[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; cb[0].descriptorCount = 1; cb[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    cb[1].binding = 1; cb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; cb[1].descriptorCount = 1; cb[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    cb[2].binding = 2; cb[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; cb[2].descriptorCount = 1; cb[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    cb[3].binding = 3; cb[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; cb[3].descriptorCount = 1; cb[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo cslc{};
    cslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    cslc.bindingCount = 4;
    cslc.pBindings = cb;
    VK(vkCreateDescriptorSetLayout(g_dev, &cslc, nullptr, &g_clDslC));
    VkDescriptorPoolSize cps[1] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}};
    VkDescriptorPoolCreateInfo cpdc{};
    cpdc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    cpdc.maxSets = 1; cpdc.poolSizeCount = 1; cpdc.pPoolSizes = cps;
    VK(vkCreateDescriptorPool(g_dev, &cpdc, nullptr, &g_clPoolC));
    VkDescriptorSetAllocateInfo cdsai{};
    cdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    cdsai.descriptorPool = g_clPoolC; cdsai.descriptorSetCount = 1; cdsai.pSetLayouts = &g_clDslC;
    VK(vkAllocateDescriptorSets(g_dev, &cdsai, &g_clSetC));
    VkDescriptorBufferInfo cbi[4] = {{g_clothBuf, 0, 16 * CN}, {g_clothBuf, 16 * CN, 16 * CN}, {g_csbuf, 0, 48 * (VkDeviceSize)g_rigid.capsuleCount()},
                                     {g_clothPBuf, 0, 40}};
    VkWriteDescriptorSet cw[4]{};
    for (int b = 0; b < 4; ++b) {
        cw[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cw[b].dstSet = g_clSetC; cw[b].dstBinding = b; cw[b].descriptorCount = 1;
        cw[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cw[b].pBufferInfo = &cbi[b];
    }
    vkUpdateDescriptorSets(g_dev, 4, cw, 0, nullptr);
    VkPipelineLayoutCreateInfo cplc{};
    cplc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    cplc.setLayoutCount = 1; cplc.pSetLayouts = &g_clDslC;
    VK(vkCreatePipelineLayout(g_dev, &cplc, nullptr, &g_clPlC));

    auto mkMod = [](const uint32_t* code, size_t n) {
        VkShaderModuleCreateInfo smc{};
        smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smc.codeSize = n * sizeof(uint32_t);
        smc.pCode = code;
        VkShaderModule m;
        VK(vkCreateShaderModule(g_dev, &smc, nullptr, &m));
        return m;
    };
    VkShaderModule cs = mkMod(reinterpret_cast<const uint32_t*>(cloth_spv), cloth_spv_len / 4);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = cs;
    stage.pName = "main";
    VkComputePipelineCreateInfo cpc{};
    cpc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpc.stage = stage;
    cpc.layout = g_clPlC;
    VK(vkCreateComputePipelines(g_dev, VK_NULL_HANDLE, 1, &cpc, nullptr, &g_clPipeC));
    vkDestroyShaderModule(g_dev, cs, nullptr);

    // render descriptor set layout: 0 pos, 1 viewProj
    VkDescriptorSetLayoutBinding rb[2]{};
    rb[0].binding = 0; rb[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; rb[0].descriptorCount = 1; rb[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    rb[1].binding = 1; rb[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; rb[1].descriptorCount = 1; rb[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo rslc{};
    rslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    rslc.bindingCount = 2; rslc.pBindings = rb;
    VK(vkCreateDescriptorSetLayout(g_dev, &rslc, nullptr, &g_clDslR));
    VkDescriptorPoolSize rps[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo rpdc{};
    rpdc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    rpdc.maxSets = 1; rpdc.poolSizeCount = 2; rpdc.pPoolSizes = rps;
    VK(vkCreateDescriptorPool(g_dev, &rpdc, nullptr, &g_clPoolR));
    VkDescriptorSetAllocateInfo rdsai{};
    rdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    rdsai.descriptorPool = g_clPoolR; rdsai.descriptorSetCount = 1; rdsai.pSetLayouts = &g_clDslR;
    VK(vkAllocateDescriptorSets(g_dev, &rdsai, &g_clSetR));
    VkDescriptorBufferInfo rbi[2] = {{g_clothBuf, 0, 16 * CN}, {g_cubuf, 0, 80}};
    VkWriteDescriptorSet rw[2]{};
    rw[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; rw[0].dstSet = g_clSetR; rw[0].dstBinding = 0; rw[0].descriptorCount = 1;
    rw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; rw[0].pBufferInfo = &rbi[0];
    rw[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; rw[1].dstSet = g_clSetR; rw[1].dstBinding = 1; rw[1].descriptorCount = 1;
    rw[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; rw[1].pBufferInfo = &rbi[1];
    vkUpdateDescriptorSets(g_dev, 2, rw, 0, nullptr);
    VkPipelineLayoutCreateInfo rplc{};
    rplc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    rplc.setLayoutCount = 1; rplc.pSetLayouts = &g_clDslR;
    VK(vkCreatePipelineLayout(g_dev, &rplc, nullptr, &g_clPlR));

    VkShaderModule vs = mkMod(reinterpret_cast<const uint32_t*>(cloth_vert_spv), cloth_vert_spv_len / 4);
    VkShaderModule fs = mkMod(reinterpret_cast<const uint32_t*>(cloth_frag_spv), cloth_frag_spv_len / 4);
    g_clPipeR = makeGraphicsPipeline(vs, fs, g_clPlR);
    vkDestroyShaderModule(g_dev, vs, nullptr);
    vkDestroyShaderModule(g_dev, fs, nullptr);
}

// Build the ball: a UV-sphere soft-body (poles + interior rings) held in the air.
// Distance constraints keep it roughly spherical; a PBD volume constraint (the pressure)
// expands/deflates it to a target volume. pos+prev SSBO + triangle index buffer + UBO.
static void createBall() {
    // Build the ball's vertices + distance constraints + triangles (sim::makeBall). The
    // SAME universal solver governs the cloth; only the constraints + the app-level
    // pressure/collision differ. Soft constraints (k=0.5) let the pressure resize it.
    std::vector<float> verts;
    std::vector<sim::Constraint> cons;
    sim::makeBall(verts, cons, g_ballTris, BLAT, BLON, BALL_R, BALL_Y0);
    g_ballInit = verts;
    float grav[3] = {0, -9.81f, 0};
    g_ballBody.init(verts.data(), BN, std::move(cons), (1.0f / 60.0f) / CLOTH_SUBSTEPS, grav, CLOTH_SUBSTEPS, 4, 0.999f);
    std::vector<uint32_t> idx;
    for (auto& t : g_ballTris) { idx.push_back(t.a); idx.push_back(t.b); idx.push_back(t.c); }
    g_ballIdxCount = (uint32_t)idx.size();
    makeBuffer(g_ballBuf, g_ballBmem, (VkDeviceSize)16 * BN * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    uploadVerts(g_ballBody, g_ballBuf, g_ballBmem);
    makeBuffer(g_ballIbo, g_ballIboMem, (VkDeviceSize)idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, idx.data());
    makeBuffer(g_blBuf, g_blUmem, 80, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);

    VkDescriptorSetLayoutBinding rb[2]{};
    rb[0].binding = 0; rb[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; rb[0].descriptorCount = 1; rb[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    rb[1].binding = 1; rb[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; rb[1].descriptorCount = 1; rb[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo rslc{};
    rslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    rslc.bindingCount = 2; rslc.pBindings = rb;
    VK(vkCreateDescriptorSetLayout(g_dev, &rslc, nullptr, &g_blDslR));
    VkDescriptorPoolSize rps[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo rpdc{};
    rpdc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    rpdc.maxSets = 1; rpdc.poolSizeCount = 2; rpdc.pPoolSizes = rps;
    VK(vkCreateDescriptorPool(g_dev, &rpdc, nullptr, &g_blPoolR));
    VkDescriptorSetAllocateInfo rdsai{};
    rdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    rdsai.descriptorPool = g_blPoolR; rdsai.descriptorSetCount = 1; rdsai.pSetLayouts = &g_blDslR;
    VK(vkAllocateDescriptorSets(g_dev, &rdsai, &g_blSetR));
    VkDescriptorBufferInfo rbi[2] = {{g_ballBuf, 0, 16 * BN}, {g_blBuf, 0, 80}};
    VkWriteDescriptorSet rw[2]{};
    rw[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; rw[0].dstSet = g_blSetR; rw[0].dstBinding = 0; rw[0].descriptorCount = 1;
    rw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; rw[0].pBufferInfo = &rbi[0];
    rw[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; rw[1].dstSet = g_blSetR; rw[1].dstBinding = 1; rw[1].descriptorCount = 1;
    rw[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; rw[1].pBufferInfo = &rbi[1];
    vkUpdateDescriptorSets(g_dev, 2, rw, 0, nullptr);
    VkPipelineLayoutCreateInfo rplc{};
    rplc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    rplc.setLayoutCount = 1; rplc.pSetLayouts = &g_blDslR;
    VK(vkCreatePipelineLayout(g_dev, &rplc, nullptr, &g_blPlR));

    auto mkMod = [](const uint32_t* code, size_t n) {
        VkShaderModuleCreateInfo smc{};
        smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smc.codeSize = n * sizeof(uint32_t);
        smc.pCode = code;
        VkShaderModule m;
        VK(vkCreateShaderModule(g_dev, &smc, nullptr, &m));
        return m;
    };
    VkShaderModule vs = mkMod(reinterpret_cast<const uint32_t*>(ball_vert_spv), ball_vert_spv_len / 4);
    VkShaderModule fs = mkMod(reinterpret_cast<const uint32_t*>(ball_frag_spv), ball_frag_spv_len / 4);
    g_blPipeR = makeGraphicsPipeline(vs, fs, g_blPlR);
    vkDestroyShaderModule(g_dev, vs, nullptr);
    vkDestroyShaderModule(g_dev, fs, nullptr);
}

// Dispatch the cloth compute shader, then barrier into the vertex stage.
// Single dispatch: each thread runs `p.substeps` Verlet sub-steps internally
// (avoids inter-dispatch barriers, which crashed the driver when repeated per frame).
static void dispatchCloth(VkCommandBuffer cmd) {
    VkBufferMemoryBarrier bar{};
    bar.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bar.buffer = g_clothBuf;
    bar.offset = 0;
    bar.size = VK_WHOLE_SIZE;
    bar.srcAccessMask = 0;
    bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &bar, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_clPipeC);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_clPlC, 0, 1, &g_clSetC, 0, nullptr);
    vkCmdDispatch(cmd, (CN + 63) / 64, 1, 1);
    bar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1, &bar, 0, nullptr);
}

 static void drawCloth(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_clPipeR);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_clPlR, 0, 1, &g_clSetR, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, g_clothIbo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, g_clothIdxCount, 1, 0, 0, 0);
}

// Upload the ball's vertices (solver -> 16-byte GPU stride) + the ball UBO (viewProj +
// centroid center, used by the ball shader for the outward normal).
static void uploadBall() {
    uploadVerts(g_ballBody, g_ballBuf, g_ballBmem);
    float cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < BN; ++i) { const float* p = g_ballBody.posPtr(i); cx += p[0]; cy += p[1]; cz += p[2]; }
    cx /= BN; cy /= BN; cz /= BN;
    void* up;
    VK(vkMapMemory(g_dev, g_blUmem, 0, 80, 0, &up));
    std::memcpy(up, g_vp.m, 64);
    std::memcpy((char*)up + 64, &cx, 4);
    std::memcpy((char*)up + 68, &cy, 4);
    std::memcpy((char*)up + 72, &cz, 4);
    vkUnmapMemory(g_dev, g_blUmem);
}
static void drawBall(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_blPipeR);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_blPlR, 0, 1, &g_blSetR, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, g_ballIbo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, g_ballIdxCount, 1, 0, 0, 0);
}

// Record + submit + wait: draw the scene bg into framebuffer `idx`, then the ImGui overlay.
void renderFrame(uint32_t idx, ImDrawData* dd) {
    void* p;
    VK(vkMapMemory(g_dev, g_umem, 0, 16, 0, &p));
    float col[4] = {g_ui.bgColor[0], g_ui.bgColor[1], g_ui.bgColor[2], 1.0f};
    std::memcpy(p, col, 16);
    vkUnmapMemory(g_dev, g_umem);

    stepCloth();                     // universal solver (Verlet + Gauss-Seidel) + one-way collision
    uploadVerts(g_clothBody, g_clothBuf, g_clothBmem);   // solver verts -> 16-byte GPU stride
    stepBall();                      // SAME solver (with gravity: the ball falls) + pressure + collision
    uploadBall();                    // upload ball verts + UBO (viewProj + centroid)

    if (vkGetFenceStatus(g_dev, g_fence) == VK_SUCCESS)
        vkResetFences(g_dev, 1, &g_fence);

    VK(vkResetCommandBuffer(g_cmd, 0));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK(vkBeginCommandBuffer(g_cmd, &cbi));
    VkClearValue cv[2]{};
    cv[0].color = {0.04f, 0.04f, 0.05f, 1.0f};
    cv[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rpb{};
    rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpb.renderPass = g_rp;
    rpb.framebuffer = g_fbs[idx];
    rpb.renderArea = {{0, 0}, g_extent};
    rpb.clearValueCount = 2;
    rpb.pClearValues = cv;
    vkCmdBeginRenderPass(g_cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vpt{0, (float)g_extent.height, (float)g_extent.width, -(float)g_extent.height, 0, 1};
    vkCmdSetViewport(g_cmd, 0, 1, &vpt);
    VkRect2D sc{0, 0, g_extent.width, g_extent.height};
    vkCmdSetScissor(g_cmd, 0, 1, &sc);
    vkCmdBindPipeline(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe);
    vkCmdBindDescriptorSets(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pl, 0, 1, &g_dset, 0, nullptr);
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(g_cmd, 0, 1, &g_vbuf, &zero);
    vkCmdDraw(g_cmd, 3, 1, 0, 0);
    drawCapsules(g_cmd);
    drawCloth(g_cmd);
    drawBall(g_cmd);
    if (dd && dd->CmdLists.Size > 0)
        ImGui_ImplVulkan_RenderDrawData(dd, g_cmd);
    vkCmdEndRenderPass(g_cmd);
    VK(vkEndCommandBuffer(g_cmd));

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_cmd;
    VK(vkQueueSubmit(g_queue, 1, &si, g_fence));
    VK(vkWaitForFences(g_dev, 1, &g_fence, VK_TRUE, 30000000000ull));
}

// Read swapchain image `idx` back to a PPM file (one-shot, for headless verification).
void readbackPPM(uint32_t idx, const std::string& path) {
    VkImageMemoryBarrier bar{};
    bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bar.image = g_images[idx];
    bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK(vkResetCommandBuffer(g_cmd, 0));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK(vkBeginCommandBuffer(g_cmd, &cbi));
    vkCmdPipelineBarrier(g_cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &bar);

    VkBuffer stbuf;
    VkDeviceMemory stmem;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = (VkDeviceSize)g_extent.width * g_extent.height * 4;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK(vkCreateBuffer(g_dev, &bci, nullptr, &stbuf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g_dev, stbuf, &mr);
    VkPhysicalDeviceMemoryProperties mpp;
    vkGetPhysicalDeviceMemoryProperties(g_pdev, &mpp);
    uint32_t mt = 0;
    for (uint32_t i = 0; i < mpp.memoryTypeCount; ++i)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mpp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
            { mt = i; break; }
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = mt;
    VK(vkAllocateMemory(g_dev, &maa, nullptr, &stmem));
    VK(vkBindBufferMemory(g_dev, stbuf, stmem, 0));

    VkBufferImageCopy r{};
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.imageOffset = {0, 0, 0};
    r.imageExtent = {g_extent.width, g_extent.height, 1};
    vkCmdCopyImageToBuffer(g_cmd, g_images[idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stbuf, 1, &r);
    VK(vkEndCommandBuffer(g_cmd));
    if (vkGetFenceStatus(g_dev, g_fence) == VK_SUCCESS)
        vkResetFences(g_dev, 1, &g_fence);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_cmd;
    VK(vkQueueSubmit(g_queue, 1, &si, g_fence));
    VK(vkWaitForFences(g_dev, 1, &g_fence, VK_TRUE, 30000000000ull));

    void* p;
    VK(vkMapMemory(g_dev, stmem, 0, mr.size, 0, &p));
    const unsigned char* px = (const unsigned char*)p;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", g_extent.width, g_extent.height);
    std::vector<unsigned char> rgb((size_t)g_extent.width * g_extent.height * 3);
    bool bgr = (g_scfmt == VK_FORMAT_B8G8R8A8_UNORM);
    for (size_t i = 0; i < (size_t)g_extent.width * g_extent.height; ++i) {
        rgb[i * 3 + 0] = bgr ? px[i * 4 + 2] : px[i * 4 + 0];
        rgb[i * 3 + 1] = px[i * 4 + 1];
        rgb[i * 3 + 2] = bgr ? px[i * 4 + 0] : px[i * 4 + 2];
    }
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
    vkUnmapMemory(g_dev, stmem);
    vkFreeMemory(g_dev, stmem, nullptr);
    vkDestroyBuffer(g_dev, stbuf, nullptr);
    // restore image layout for future use
    bar.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.dstAccessMask = 0;
    bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VK(vkResetCommandBuffer(g_cmd, 0));
    VK(vkBeginCommandBuffer(g_cmd, &cbi));
    vkCmdPipelineBarrier(g_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    VK(vkEndCommandBuffer(g_cmd));
    if (vkGetFenceStatus(g_dev, g_fence) == VK_SUCCESS)
        vkResetFences(g_dev, 1, &g_fence);
    si.commandBufferCount = 1;
    VK(vkQueueSubmit(g_queue, 1, &si, g_fence));
    VK(vkWaitForFences(g_dev, 1, &g_fence, VK_TRUE, 30000000000ull));
    std::printf("wrote %s\n", path.c_str());
}

// Headless ball physics test: build the ball's soft body (like createBall, no Vulkan) and
// step it for N frames (60 frames/s; pressure + ground collision every frame). Verifies
// the ball FALLS and lands, and keeps its volume, without needing a window.
static void ballTestFrames(int frames) {
    std::vector<float> verts;
    std::vector<sim::Constraint> cons;
    std::vector<sim::Triangle> tris;
    sim::makeBall(verts, cons, tris, BLAT, BLON, BALL_R, BALL_Y0);
    sim::SoftBody body;
    float grav[3] = {0, -9.81f, 0};
    body.init(verts.data(), BN, std::move(cons), (1.0f / 60.0f) / CLOTH_SUBSTEPS, grav, CLOTH_SUBSTEPS, 4, 0.999f);
    const float vRest = 4.0f / 3.0f * 3.14159265f * BALL_R * BALL_R * BALL_R;
    const float vTarget = vRest * g_ui.ballPressure;
    for (int f = 0; f < frames; ++f) {
        body.gravity[1] = -9.81f;   // the ball falls
        body.step();
        sim::applyPressure(body, tris, vTarget);
        sim::applyCollision(body, {});   // no capsules here -> ground only
    }
    float cx = 0, cy = 0, cz = 0, ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < BN; ++i) {
        const float* p = body.posPtr(i);
        cx += p[0]; cy += p[1]; cz += p[2];
        if (p[1] < ymin) ymin = p[1];
        if (p[1] > ymax) ymax = p[1];
    }
    cx /= BN; cy /= BN; cz /= BN;
    std::fprintf(stderr, "[balltest %d frames] centroid=(%.2f,%.2f,%.2f) y=[%.2f,%.2f] vol=%.2fx rest\n",
                 frames, cx, cy, cz, ymin, ymax, sim::bodyVolume(body, tris) / vRest);
}

int main(int argc, char** argv) {
    bool shot = false;
    std::string shotPath = "shot.ppm";
    int shotSteps = 150;
    int ballTest = 0;   // >0: headless ball physics test (frames)
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--shot") {
            shot = true;
            if (i + 1 < argc)
                shotPath = argv[++i];
        } else if (std::string(argv[i]) == "--steps") {
            if (i + 1 < argc)
                shotSteps = std::atoi(argv[++i]);
        } else if (std::string(argv[i]) == "--balltest") {
            if (i + 1 < argc)
                ballTest = std::atoi(argv[++i]);
        }
    if (ballTest > 0) {
        ballTestFrames(ballTest);
        return 0;
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(900, 900, "3dsim", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "window creation failed\n");
        return 1;
    }

    uint32_t n = 0;
    const char** req = glfwGetRequiredInstanceExtensions(&n);
    std::vector<const char*> layers;
    if (std::getenv("VK_VALIDATE")) {
        uint32_t nl = 0;
        vkEnumerateInstanceLayerProperties(&nl, nullptr);
        std::vector<VkLayerProperties> lprops(nl);
        vkEnumerateInstanceLayerProperties(&nl, lprops.data());
        for (auto& p : lprops)
            if (std::string(p.layerName) == "VK_LAYER_KHRONOS_validation")
                layers.push_back(p.layerName);
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "3dsim";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ic{};
    ic.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ic.pApplicationInfo = &app;
    ic.enabledExtensionCount = n;
    ic.ppEnabledExtensionNames = req;
    ic.enabledLayerCount = (uint32_t)layers.size();
    ic.ppEnabledLayerNames = layers.data();
    VK(vkCreateInstance(&ic, nullptr, &g_inst));

    VK(glfwCreateWindowSurface(g_inst, win, nullptr, &g_surface));

    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(g_inst, &nd, nullptr);
    std::vector<VkPhysicalDevice> devs(nd);
    vkEnumeratePhysicalDevices(g_inst, &nd, devs.data());
    for (auto d : devs) {
        uint32_t qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qps(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, qps.data());
        for (uint32_t i = 0; i < qc; ++i) {
            VkBool32 present = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, g_surface, &present);
            if ((qps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                g_pdev = d;
                g_qf = i;
                break;
            }
        }
        if (g_pdev)
            break;
    }
    if (!g_pdev) {
        std::fprintf(stderr, "no graphics+present device\n");
        return 1;
    }

    const char* devExt = "VK_KHR_swapchain";
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_qf;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = &devExt;
    VK(vkCreateDevice(g_pdev, &dci, nullptr, &g_dev));
    vkGetDeviceQueue(g_dev, g_qf, 0, &g_queue);

    VkSurfaceCapabilitiesKHR caps;
    VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_pdev, g_surface, &caps));
    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_pdev, g_surface, &nf, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(nf);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_pdev, g_surface, &nf, fmts.data());
    VkSurfaceFormatKHR fmt = fmts[0];
    g_scfmt = fmt.format;
    g_extent = caps.currentExtent;
    if (g_extent.width == 0xFFFFFFFF) {
        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        g_extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }
    uint32_t icnt = caps.minImageCount + 1;
    if (caps.maxImageCount && icnt > caps.maxImageCount)
        icnt = caps.maxImageCount;
    VkSwapchainCreateInfoKHR scc{};
    scc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scc.surface = g_surface;
    scc.minImageCount = icnt;
    scc.imageFormat = fmt.format;
    scc.imageColorSpace = fmt.colorSpace;
    scc.imageExtent = g_extent;
    scc.imageArrayLayers = 1;
    scc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scc.preTransform = caps.currentTransform;
    scc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scc.clipped = VK_TRUE;
    VK(vkCreateSwapchainKHR(g_dev, &scc, nullptr, &g_sc));
    vkGetSwapchainImagesKHR(g_dev, g_sc, &icnt, nullptr);
    g_images.resize(icnt);
    vkGetSwapchainImagesKHR(g_dev, g_sc, &icnt, g_images.data());

    // depth (shared by all framebuffers)
    createDepth(g_extent);

    // render pass (color + depth)
    VkAttachmentDescription att[2]{};
    att[0].format = fmt.format;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    att[1].format = VK_FORMAT_D32_SFLOAT;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference cr[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription sp{};
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &cr[0];
    sp.pDepthStencilAttachment = &cr[1];
    VkRenderPassCreateInfo rpc{};
    rpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpc.attachmentCount = 2;
    rpc.pAttachments = att;
    rpc.subpassCount = 1;
    rpc.pSubpasses = &sp;
    VK(vkCreateRenderPass(g_dev, &rpc, nullptr, &g_rp));

    // image views + framebuffers
    g_views.resize(icnt);
    g_fbs.resize(icnt);
    for (uint32_t i = 0; i < icnt; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = g_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt.format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK(vkCreateImageView(g_dev, &vci, nullptr, &g_views[i]));
        VkImageView fatts[2] = {g_views[i], g_depthView};
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = g_rp;
        fci.attachmentCount = 2;
        fci.pAttachments = fatts;
        fci.width = g_extent.width;
        fci.height = g_extent.height;
        fci.layers = 1;
        VK(vkCreateFramebuffer(g_dev, &fci, nullptr, &g_fbs[i]));
    }

    VkCommandPoolCreateInfo cpc{};
    cpc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpc.queueFamilyIndex = g_qf;
    VK(vkCreateCommandPool(g_dev, &cpc, nullptr, &g_pool));
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = g_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VK(vkAllocateCommandBuffers(g_dev, &cai, &g_cmd));

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK(vkCreateFence(g_dev, &fci, nullptr, &g_fence));

    createScenePipeline(fmt.format);

    // fixed camera looking at the capsule field
    {
        float aspect = (float)g_extent.width / (float)g_extent.height;
        Mat4 proj = perspective(50.0f, aspect, 0.1f, 300.0f);
        Mat4 view = lookAt({0.0f, 9.0f, 14.0f}, {0.0f, 3.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
        g_vp = mul4(proj, view);
    }
    g_rigid.init();
    createCapsulePipeline();
    createCloth();
    createBall();

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(win, true);
    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_1;
    ii.Instance = g_inst;
    ii.PhysicalDevice = g_pdev;
    ii.Device = g_dev;
    ii.QueueFamily = g_qf;
    ii.Queue = g_queue;
    ii.DescriptorPoolSize = 1024;
    ii.MinImageCount = icnt;
    ii.ImageCount = icnt;
    ii.PipelineInfoMain.RenderPass = g_rp;
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.CheckVkResultFn = [](VkResult err) { if (err != VK_SUCCESS) std::printf("ImGui VK err %d\n", (int)err); };
    ImGui_ImplVulkan_Init(&ii);

    if (shot) {
        g_clothPinned = 0;        // drop the cloth the whole shot so it ends draped
        g_clothSubsteps = std::min(shotSteps * CLOTH_SUBSTEPS, 600);
        stepRigid(shotSteps);
        uploadCapsules();
        glfwPollEvents();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        {
            const float vRest = 4.0f / 3.0f * 3.14159265f * BALL_R * BALL_R * BALL_R;
            float ballVolRatio = sim::bodyVolume(g_ballBody, g_ballTris) / vRest;
            bool clothReset = false, ballReset = false;
            drawOverlay(g_ui, g_clothPinned, ballVolRatio, clothReset, ballReset);
        }
        ImGui::Render();
        renderFrame(0, ImGui::GetDrawData());
        readbackPPM(0, shotPath);
    } else {
        std::printf("3dsim: vulkan+imgui | R to reset | esc/close to quit\n");
        while (!glfwWindowShouldClose(win)) {
            if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS)
                resetCloth();
            stepRigid(1);
            if (g_steps >= CLOTH_HOLD)
                g_clothPinned = 0;
            g_clothSubsteps = CLOTH_SUBSTEPS;
            uploadCapsules();
            glfwPollEvents();
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            {
                const float vRest = 4.0f / 3.0f * 3.14159265f * BALL_R * BALL_R * BALL_R;
                float ballVolRatio = sim::bodyVolume(g_ballBody, g_ballTris) / vRest;
                bool clothReset = false, ballReset = false;
                drawOverlay(g_ui, g_clothPinned, ballVolRatio, clothReset, ballReset);
                if (clothReset)
                    resetCloth();
                if (ballReset)
                    g_ballBody.reset(g_ballInit.data());
            }
            ImGui::Render();

            uint32_t idx = 0;
            VK(vkAcquireNextImageKHR(g_dev, g_sc, UINT64_MAX, VK_NULL_HANDLE, g_fence, &idx));
            renderFrame(idx, ImGui::GetDrawData());
            VkPresentInfoKHR pi{};
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.waitSemaphoreCount = 0;
            pi.swapchainCount = 1;
            pi.pSwapchains = &g_sc;
            pi.pImageIndices = &idx;
            VK(vkQueuePresentKHR(g_queue, &pi));
        }
    }

    vkDeviceWaitIdle(g_dev);
    vkDestroyPipeline(g_dev, g_pipe, nullptr);
    vkDestroyPipelineLayout(g_dev, g_pl, nullptr);
    vkDestroyBuffer(g_dev, g_ubuf, nullptr);
    vkFreeMemory(g_dev, g_umem, nullptr);
    vkDestroyDescriptorSetLayout(g_dev, g_dsl, nullptr);
    vkDestroyDescriptorPool(g_dev, g_dpool, nullptr);
    vkDestroyCommandPool(g_dev, g_pool, nullptr);
    vkDestroyFence(g_dev, g_fence, nullptr);
    for (uint32_t i = 0; i < g_fbs.size(); ++i) {
        vkDestroyFramebuffer(g_dev, g_fbs[i], nullptr);
        vkDestroyImageView(g_dev, g_views[i], nullptr);
    }
    vkDestroyRenderPass(g_dev, g_rp, nullptr);
    vkDestroySwapchainKHR(g_dev, g_sc, nullptr);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroySurfaceKHR(g_inst, g_surface, nullptr);
    vkDestroyDevice(g_dev, nullptr);
    vkDestroyInstance(g_inst, nullptr);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
