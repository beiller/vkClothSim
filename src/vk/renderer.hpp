#pragma once
#include <vulkan/vulkan.h>

#include "components.hpp"
#include "gpuverts.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include "xr_frame.hpp"
#include <entt/entt.hpp>
#include <functional>
#include <imgui.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class VkApp;

struct CameraView {
    Mat4 viewProj;
    Mat4 view;
    V3 pos;
    float tanL, tanR, tanU, tanD;
    float zNear, zFar;
};

class Renderer {
public:
    static constexpr int kMaxLights = 8;
    static constexpr int kPoolChunk = 64;

    void init(VkApp& app, int nInstances, const Mat4& viewProj);
    // sole owner of drawable vertex buffers; sim stages request a read-write store on adoption
    GpuMeshRef requestVerts(const entt::registry& reg, const Mesh& mesh, entt::entity keyEnt);
    int addMesh(const Mesh& mesh);
    int instFor(entt::entity e, entt::entity geomEnt, const entt::registry& reg);
    int instOf(entt::entity e) const;
    int textureId(entt::entity e) const;
    int textureFor(entt::entity e, const entt::registry& reg);
    void addTexture(entt::entity e, const void* rgba, uint32_t w, uint32_t h);
    void watch(entt::registry& reg);
    void flushFrees();
    void reset();
    void setEnvironment(VkApp& app, const float* rgb, uint32_t w, uint32_t h);
    void setViewProj(const CameraView& cv);
    void shutdown();
    void draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const entt::registry& reg, const RenderSettings& rs,
              ImDrawData* imgui);

    void initXrTarget(VkApp& app, VkFormat fmt, int nEyes, const std::vector<std::vector<VkImage>>& images,
                      const VkExtent2D* exts);
    void drawVr(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const entt::registry& reg, const RenderSettings& rs,
                ImDrawData* imgui, entt::entity cam, const uint32_t* xrImg, int nXr);

private:
    void drawXrEye(VkCommandBuffer cmd, VkApp& app, int eye, uint32_t imgIdx);
    struct GpuMesh {
        VkBuffer pos = VK_NULL_HANDLE, nrm = VK_NULL_HANDLE, uv = VK_NULL_HANDLE;
        VkDeviceMemory posMem = VK_NULL_HANDLE, nrmMem = VK_NULL_HANDLE, uvMem = VK_NULL_HANDLE;
        VkBuffer ibuf = VK_NULL_HANDLE;
        VkDeviceMemory ibmem = VK_NULL_HANDLE;
        uint32_t idxCount = 0;
        uint32_t vtxCount = 0;
    };

    struct GpuTexture {
        VkImage img = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    struct InstancedMesh {
        int geom = -1;
        bool dead = false;
        VkBuffer modelUbuf = VK_NULL_HANDLE;
        VkDeviceMemory modelMem = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSet setTrans = VK_NULL_HANDLE;
        int tex[4] = {-2, -2, -2, -2};
        int alphaMode = 0;
        float refraction = 0.0f;
        bool visible = true;
        V3 wpos{};
    };

    struct EnvPass {
        VkPipeline pipe = VK_NULL_HANDLE;
        VkPipelineLayout pl = VK_NULL_HANDLE;
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    static void drawInstance(VkCommandBuffer cmd, VkPipelineLayout pl, const GpuMesh& g, const VkDescriptorSet set);
    int allocInstance(int geom);
    void queueFreeInst(int inst);
    int geomFor(entt::entity g, const entt::registry& reg);
    void onRenderableDestroyed(entt::registry&, entt::entity e);
    void onTextureDestroyed(entt::registry&, entt::entity e);
    void onGeometryDestroyed(entt::registry&, entt::entity e);
    void rebuildTonemapSet(VkApp& app);
    void releaseEnv();
    void writeViewUbo(const CameraView& cv, float envIntensity, VkExtent2D ext);
    void writeLights(const entt::registry& reg);
    void prepBuffers(const entt::registry& reg);
    void renderSceneBuffers(VkCommandBuffer cmd, VkApp& app, const float bg[3], const CameraView& cv);
    void renderOpaquePass(VkCommandBuffer cmd, VkApp& app, const float bg[3]);
    void renderTransparentPass(VkCommandBuffer cmd, VkApp& app, const V3& camPos);
    void renderSssBlur(VkCommandBuffer cmd, VkApp& app, const CameraView& cv);
    void setTexture(int inst, int channel, int texId);
    void updateSceneSamplers(VkApp& app);
    void ensureSceneSamplers(VkApp& app);
    void hdrBarrier(VkCommandBuffer cmd, VkApp& app);
    void initSss(VkApp& app);
    void releaseSss();
    void writeTonemapUbo(float ox, float oy, float sx, float sy);
    void drawTonemapRegion(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const VkRect2D& region, bool clear);
    void ensureNoClearFbs(VkApp& app);
    Mat4 eyeVP(const View& v) const;
    VkRect2D eyeRegion(const View& v, VkExtent2D ext, int eye) const;
    void initShadow(VkApp& app);
    void renderShadowCubes(VkCommandBuffer cmd, const entt::registry& reg);
    void makeComputePass(const void* spv, uint32_t len, const std::vector<VkDescriptorSetLayoutBinding>& binds,
                         uint32_t pcSize, EnvPass& out, uint32_t maxSets = 1);
    void writeEnvSet(VkDescriptorSet set);

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    VkRenderPass m_rp = VK_NULL_HANDLE;

    VkBuffer m_vpUbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_vpMem = VK_NULL_HANDLE;
    VkBuffer m_dbgBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_dbgMem = VK_NULL_HANDLE;
    uint64_t m_dbgSize = 0;
    bool m_dbgReadScene = false;
    VkDescriptorSetLayout m_dsl = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_meshPools;
    int m_poolCap = 0, m_poolUsed = 0;
    VkPipelineLayout m_pl = VK_NULL_HANDLE;
    VkPipeline m_pipe = VK_NULL_HANDLE;
    VkPipeline m_pipeBlend = VK_NULL_HANDLE;
    VkPipeline m_pipeBlendCull = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_tmDsl = VK_NULL_HANDLE;
    VkDescriptorPool m_tmPool = VK_NULL_HANDLE;
    VkDescriptorSet m_tmSet = VK_NULL_HANDLE;
    VkPipelineLayout m_tmPl = VK_NULL_HANDLE;
    VkPipeline m_tmPipe = VK_NULL_HANDLE;
    VkBuffer m_tmUbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_tmMem = VK_NULL_HANDLE;
    VkImageView m_tmSrcView = VK_NULL_HANDLE;

    static constexpr int kTexChannels = 4; // albedo, metallicRoughness, normal, emissive

    std::vector<GpuMesh> m_geoms;
    std::vector<InstancedMesh> m_insts;
    std::vector<GpuTexture> m_texs;
    std::unordered_map<entt::entity, int> m_entInst;
    std::unordered_map<entt::entity, int> m_entTex;
    std::unordered_map<entt::entity, int> m_entGeom;
    std::unordered_set<entt::entity> m_geomWarned, m_texWarned;
    std::vector<int> m_freeInst, m_freeTex;
    GpuTexture m_white;
    GpuTexture m_neutralNrm;
    VkSampler m_texSampler = VK_NULL_HANDLE;

    GpuTexture m_envEq, m_envPre, m_envIrr;
    uint32_t m_envEqW = 0, m_envEqH = 0;
    std::vector<VkImageView> m_envPreMips;
    VkImageView m_envIrrArr = VK_NULL_HANDLE;
    VkSampler m_eqSampler = VK_NULL_HANDLE, m_cubeSampler = VK_NULL_HANDLE;
    EnvPass m_pcCube, m_pcPref, m_pcIrr;
    VkDescriptorImageInfo m_envSpecInfo{}, m_envIrrInfo{};
    bool m_envReady = false;

    CameraView m_camView{};
    float m_exposure = 1.0f;
    VkBuffer m_lightsUbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_lightsMem = VK_NULL_HANDLE;

    EnvPass m_sss;
    VkDescriptorSet m_sssSet[2] = {};
    float m_sssScale = 0.05f;         // Godot subsurface_scattering_scale default
    float m_sssDepthScale = 0.01f;    // Godot subsurface_scattering_depth_scale default
    VkSampler m_sceneColorSampler = VK_NULL_HANDLE; // CLAMP + linear mip (roughness blur = rough * 8)
    VkSampler m_depthCopySampler = VK_NULL_HANDLE;
    VkImageView m_sssSceneView = VK_NULL_HANDLE;
    VkImageView m_sssTempView = VK_NULL_HANDLE;
    VkImageView m_sssDepthView = VK_NULL_HANDLE;
    VkImageView m_sssDepthCopyView = VK_NULL_HANDLE;
    VkImageView m_sssDiffuseView = VK_NULL_HANDLE;
    bool m_hasSss = false;
    bool m_hasGlass = false;
    bool m_hasTrans = false;
    bool m_samplersDirty = true;
    VkImageView m_hdrView = VK_NULL_HANDLE;
    bool m_hdrFirst = true;

    VkRenderPass m_noClearRp = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_noClearFbs;
    VkExtent2D m_noClearExt{};
    bool m_xrReady = false;
    VkFormat m_xrFmt = VK_FORMAT_UNDEFINED;
    VkRenderPass m_xrRp = VK_NULL_HANDLE;
    VkPipeline m_xrPipe = VK_NULL_HANDLE;
    VkBuffer m_xrTmUbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_xrTmMem = VK_NULL_HANDLE;
    VkDescriptorSet m_xrSet = VK_NULL_HANDLE;
    std::vector<std::vector<VkImageView>> m_xrViews;
    std::vector<std::vector<VkFramebuffer>> m_xrFbs;
    VkExtent2D m_xrExt[2]{};
    VkImage m_shadowCube = VK_NULL_HANDLE, m_shadowDepth = VK_NULL_HANDLE;
    VkDeviceMemory m_shadowCubeMem = VK_NULL_HANDLE, m_shadowDepthMem = VK_NULL_HANDLE;
    VkImageView m_shadowSampleView = VK_NULL_HANDLE;
    VkImageView m_shadowColorFace[kMaxLights * 6] = {};
    VkImageView m_shadowDepthFace[6] = {};
    VkFramebuffer m_shadowFb[kMaxLights * 6] = {};
    VkRenderPass m_shadowRp = VK_NULL_HANDLE;
    VkPipeline m_shadowPipe = VK_NULL_HANDLE;
    VkPipelineLayout m_shadowPl = VK_NULL_HANDLE;
    VkSampler m_shadowSampler = VK_NULL_HANDLE;
    VkDescriptorImageInfo m_shadowInfo{};
    uint32_t m_shadowSize = 256;
    float m_shadowRange = 30.0f;
};
