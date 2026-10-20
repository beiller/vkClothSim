#pragma once
#include <vulkan/vulkan.h>

#include "gpuverts.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include "xr_frame.hpp"
#include <entt/entt.hpp>
#include <imgui.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class VkApp;

// renderer-side state the UI binds to (not scene-graph data, so plain state, not an entity)
struct RenderSettings {
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    float exposure = 1.0f;
    float envIntensity = 1.0f;
};

// a camera's full view data: the SSS/glass passes need the view matrix + frustum, not just
// the combined viewProj; the signed tangents match the XR View convention
struct CameraView {
    Mat4 viewProj;
    Mat4 view;
    V3 pos;
    float tanL, tanR, tanU, tanD;
    float zNear, zFar;
};

class Renderer {
public:
    struct GpuMeshRef {
        int geom;
        MeshGpu rw;
    };

    static constexpr int kMaxLights = 8;
    static constexpr int kPoolChunk = 64; // descriptor pool growth step for mesh instances

    void init(VkApp& app, int nInstances, const Mat4& viewProj);
    GpuMeshRef addMesh(const Mesh& mesh);
    // a GpuMesh whose pos/nrm buffers are owned by the Skin (freed with the last aliasing
    // mesh): the renderer uploads only the uv + index buffers and aliases the rest; the prim
    // entity IS the geometry (m_entGeom key)
    void addSharedMesh(entt::entity geomEnt, const Mesh& m, VkBuffer pos, VkDeviceMemory posMem, VkBuffer nrm,
                       VkDeviceMemory nrmMem, uint32_t n);
    // geometry entity -> an existing slot whose pos/nrm buffers the soft sim reads and writes
    // (one buffer, two users; the slot owns it, as the old soft.body path did)
    void shareGeomSlot(entt::entity geomEnt, int slot);
    // the entity's instance slot (per-instance model UBO + descriptor set); vertex data uploads
    // lazily on first sight, the slot is freed one frame after the Renderable dies (flushFrees).
    // -1 when the geometry is missing
    int instFor(entt::entity e, entt::entity geomEnt, const entt::registry& reg);
    int instOf(entt::entity e) const; // -1 when the entity has no live instance
    int textureId(entt::entity e) const; // -1 when the entity has no uploaded texture
    // the texture id for an entity, uploading its Texture{path} file lazily on first sight
    int textureFor(entt::entity e, const entt::registry& reg);
    void addTexture(entt::entity e, const void* rgba, uint32_t w, uint32_t h);
    // register the destroy observers: Renderable/Texture/Geometry teardowns queue their
    // resources for the next frame-start flush (the previous frame may still be reading them)
    void watch(entt::registry& reg);
    void flushFrees();
    // clear the per-scene content (instance/geometry/texture slots, the env maps, the entity
    // maps) between loads; the fixed state (pipelines, passes, pools, xr target) persists
    void reset();
    void setEnvironment(VkApp& app, const float* rgb, uint32_t w, uint32_t h);
    void setViewProj(const CameraView& cv);
    void shutdown();
    // per-frame: query the ECS for instance poses/materials + lights, upload them, render
    void draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const entt::registry& reg, const RenderSettings& rs,
              ImDrawData* imgui);

    // VR: per-eye scene render + side-by-side window debug view + headset swapchain. The eyes are
    // the views on the stereo camera's Camera component (filled by stepXr each frame)
    void initXrTarget(VkApp& app, VkFormat fmt, int nEyes, const std::vector<std::vector<VkImage>>& images,
                      const VkExtent2D* exts);
    // xrImg[i] is the acquired swapchain image index for eye i (headet); nullptr = window only
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
        bool externalPosNrm = false; // pos/nrm owned by the Skeleton; the free path skips them
    };

    struct GpuTexture {
        VkImage img = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    struct InstancedMesh {
        int geom = -1;
        bool dead = false; // queued for the frame-start flush; never drawn or reused
        VkBuffer modelUbuf = VK_NULL_HANDLE;
        VkDeviceMemory modelMem = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSet setTrans = VK_NULL_HANDLE; // same set, but 2/15 are the scene samplers (the
                                                 // glass); the opaque set holds a neutral there
        int tex[4] = {-2, -2, -2, -2}; // per-channel texture id last written to the descriptor
        int alphaMode = 0;             // material alpha mode (2 = blend -> drawn in the sorted pass)
        float refraction = 0.0f;       // > 0 -> drawn in the sorted (transparent) pass (glass)
        bool visible = true;           // the Renderable's visible flag (prepBuffers)
        V3 wpos{};                     // world position (sort key for the blend pass)
    };

    struct EnvPass {
        VkPipeline pipe = VK_NULL_HANDLE;
        VkPipelineLayout pl = VK_NULL_HANDLE;
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    static void drawInstance(VkCommandBuffer cmd, VkPipelineLayout pl, const GpuMesh& g, const VkDescriptorSet set);
    int allocInstance(int geom); // create the UBO + descriptor set slot
    void queueFreeInst(int inst); // mark dead + queue for flushFrees
    int geomFor(entt::entity g, const entt::registry& reg); // geometry entity -> m_geoms index (lazy upload)
    void onRenderableDestroyed(entt::registry&, entt::entity e); // destroy-observer handlers
    void onTextureDestroyed(entt::registry&, entt::entity e);
    void onGeometryDestroyed(entt::registry&, entt::entity e);
    void rebuildTonemapSet(VkApp& app);
    void releaseEnv(); // the IBL env maps + passes (m_envReady-guarded, idempotent)
    void writeViewUbo(const CameraView& cv, float envIntensity, VkExtent2D ext);
    void writeLights(const entt::registry& reg);
    void prepBuffers(const entt::registry& reg);
    // the frame sequence (window: once, VR: per eye): opaque -> [SSS blur] -> [mips] -> blit to
    // HDR -> transparent (glass) -> returns; the caller does the tonemap
    void renderSceneBuffers(VkCommandBuffer cmd, VkApp& app, const float bg[3], const CameraView& cv);
    void renderOpaquePass(VkCommandBuffer cmd, VkApp& app, const float bg[3]);
    void renderTransparentPass(VkCommandBuffer cmd, VkApp& app, const V3& camPos);
    void renderSssBlur(VkCommandBuffer cmd, VkApp& app, const CameraView& cv);
    void setTexture(int inst, int channel, int texId); // rewrite the descriptor only when the id changes
    void updateSceneSamplers(VkApp& app); // re-write the view-shared scene samplers after a resize
    void ensureSceneSamplers(VkApp& app); // update once, before any pass binds the instance sets
    void hdrBarrier(VkCommandBuffer cmd, VkApp& app);
    void initSss(VkApp& app);
    void releaseSss();
    void writeTonemapUbo(float ox, float oy, float sx, float sy);
    // tonemap the HDR buffer into a swapchain-image region (no EndRenderPass; the caller draws
    // imgui and closes the pass); clear = first eye gets the clearing pass
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
    // DBG: one-shot HDR readback
    VkBuffer m_dbgBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_dbgMem = VK_NULL_HANDLE;
    uint64_t m_dbgSize = 0;
    bool m_dbgReadScene = false;
    VkDescriptorSetLayout m_dsl = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_meshPools; // grown in kPoolChunk steps as instances are added
    int m_poolCap = 0, m_poolUsed = 0;
    VkPipelineLayout m_pl = VK_NULL_HANDLE;
    VkPipeline m_pipe = VK_NULL_HANDLE;
    VkPipeline m_pipeBlend = VK_NULL_HANDLE;
    VkPipeline m_pipeBlendCull = VK_NULL_HANDLE; // glass: the back face is culled (no depth write,
                                               // so the two-sided blend order is undefined)

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
    std::unordered_map<entt::entity, int> m_entInst; // Renderable entity -> m_insts index
    std::unordered_map<entt::entity, int> m_entTex;  // Texture entity -> m_texs index
    std::unordered_map<entt::entity, int> m_entGeom; // geometry entity -> m_geoms index (lazy upload)
    std::unordered_set<entt::entity> m_geomWarned, m_texWarned; // missing-data warnings, once each
    std::vector<int> m_freeInst, m_freeTex;          // slots awaiting the frame-start flush
    GpuTexture m_white;        // fallback for albedo/MR/emissive (texel 1 = factor only)
    GpuTexture m_neutralNrm;   // fallback for the normal channel (texel (0.5,0.5,1) = identity)
    VkSampler m_texSampler = VK_NULL_HANDLE;

    GpuTexture m_envEq, m_envPre, m_envIrr;
    uint32_t m_envEqW = 0, m_envEqH = 0;
    std::vector<VkImageView> m_envPreMips;
    VkImageView m_envIrrArr = VK_NULL_HANDLE;
    VkSampler m_eqSampler = VK_NULL_HANDLE, m_cubeSampler = VK_NULL_HANDLE;
    EnvPass m_pcCube, m_pcPref, m_pcIrr;
    VkDescriptorImageInfo m_envSpecInfo{}, m_envIrrInfo{};
    bool m_envReady = false;

    CameraView m_camView{}; // window camera (setViewProj); VR writes per-eye UBOs directly
    float m_exposure = 1.0f;
    VkBuffer m_lightsUbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_lightsMem = VK_NULL_HANDLE;

    // SSS: the Godot-style screen-space blur (two separable compute passes over sceneColor/temp)
    EnvPass m_sss;
    VkDescriptorSet m_sssSet[2] = {}; // pass 1: sceneColor -> temp; pass 2: temp -> sceneColor
    float m_sssScale = 0.05f;         // Godot subsurface_scattering_scale default
    float m_sssDepthScale = 0.01f;    // Godot subsurface_scattering_depth_scale default
    VkSampler m_sceneColorSampler = VK_NULL_HANDLE; // CLAMP + linear mip (roughness blur = rough * 8)
    VkSampler m_depthCopySampler = VK_NULL_HANDLE;
    VkImageView m_sssSceneView = VK_NULL_HANDLE; // the views the last set update bound (resize check)
    VkImageView m_sssTempView = VK_NULL_HANDLE;
    VkImageView m_sssDepthView = VK_NULL_HANDLE;
    VkImageView m_sssDepthCopyView = VK_NULL_HANDLE;
    bool m_hasSss = false; // prepBuffers: any ssssWeight > 0
    bool m_hasGlass = false; // prepBuffers: any refraction > 0
    bool m_hasTrans = false; // prepBuffers: any alphaMode == 2 or glass (the sorted pass runs)
    bool m_samplersDirty = true; // a per-instance set was created since the last sampler update
    VkImageView m_hdrView = VK_NULL_HANDLE; // the hdr view the last blit sequence transitioned
    bool m_hdrFirst = true; // a fresh hdr image starts in UNDEFINED

    // VR state
    VkRenderPass m_noClearRp = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_noClearFbs;
    VkExtent2D m_noClearExt{};
    // headset swapchain target
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
