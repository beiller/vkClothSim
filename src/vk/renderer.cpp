#include "vk/renderer.hpp"

#include "assets.hpp"
#include "ecs.hpp"
#include "mesh_frag_spv.hpp"
#include "mesh_vert_spv.hpp"
#include "sss_blur_spv.hpp"
#include "tonemap_frag_spv.hpp"
#include "tonemap_vert_spv.hpp"
#include "vk/vkapp.hpp"
#include "vk/vkutil.hpp"
#include <algorithm>
#include <cstring>
#include <imgui_impl_vulkan.h>

namespace {

struct ModelUbo {
    float model[16];    // 0
    float baseColor[3]; // 64
    float metallic;     // 76
    float roughness;    // 80
    float baseAlpha;    // 84
    float alphaCutoff;  // 88
    float alphaMode;    // 92  0 opaque, 1 mask, 2 blend
    float emissive[3];  // 96
    float ssssWeight;   // 108
    float refraction;   // 112
    float pad[3];       // 116
};
static_assert(sizeof(ModelUbo) == 128, "ModelUbo must match the shader std140 layout");

// the SSS/glass passes need the view matrix + frustum, not just viewProj (screen-space
// refraction reconstructs view-space positions); screenSize + zNear/zFar linearize the depth
struct ViewUbo {
    float viewProj[16]; // 0
    float view[16];     // 64
    float camPos[3];    // 128 (std140: a vec3 is 12 bytes; the next scalar lands at 140)
    float envIntensity; // 140
    float shadowTexels; // 144
    float pad[3];       // 148 (pad so the 16-aligned frustum lands at 160)
    float frustum[4];   // 160 tanL tanR tanU tanD
    float zNear;        // 176
    float zFar;         // 180
    float screenSize[2];// 184
    float pad2[4];      // 192
};
static_assert(sizeof(ViewUbo) == 208, "ViewUbo must match the shader std140 layout");

// the SSS compute's push constants (std430: scalars are 4-byte aligned)
struct SssPc {
    float screenSize[2]; // 0
    float zNear;         // 8
    float zFar;          // 12
    float unitSize;      // 16
    float scale;         // 20
    float depthScale;    // 24
    float vertical;      // 28  1 = vertical pass (dir 0,1), 0 = horizontal
};
static_assert(sizeof(SssPc) == 32, "SssPc must match the shader std430 layout");

// Godot's unit_size: p_camera.xform4(Plane(1, 0, -1, 1)), p.normal /= p.d, p.normal.x, where
// p_camera is the pure perspective projection (the view transform is kept separate in Godot);
// for a perspective matrix this reduces to 2 / (tanR - tanL) (1 for a unit-tan symmetric view)
float unitSizeOf(const CameraView& cv) {
    return 2.0f / (cv.tanR - cv.tanL);
}

struct LightGPU {
    float pos[3];
    float intensity;
    float color[3];
    float radius;
    float on;
    float shadowNear;
    float shadowFar;
    float shadowNormalBias;
    float biasBase;
    float biasSlope;
    float searchScale;
    float maxRadius;
};
static_assert(sizeof(LightGPU) == 64, "LightGPU must match the shader std430 layout");

struct LightsUbo {
    int count;
    float pad[3];
    LightGPU lights[Renderer::kMaxLights];
};
static_assert(sizeof(LightsUbo) == 16 + Renderer::kMaxLights * 64, "LightsUbo must match the shader std430 layout");

ModelUbo packModel(const Mat4& model, const V3& baseColor, float metallic, float roughness, const V3& emissive,
                   float baseAlpha, float alphaCutoff, float alphaMode, float ssssWeight, float refraction) {
    ModelUbo mb{};
    std::memcpy(mb.model, model.m, 64);
    mb.baseColor[0] = baseColor.x;
    mb.baseColor[1] = baseColor.y;
    mb.baseColor[2] = baseColor.z;
    mb.metallic = metallic;
    mb.roughness = roughness;
    mb.baseAlpha = baseAlpha;
    mb.alphaCutoff = alphaCutoff;
    mb.alphaMode = alphaMode;
    mb.emissive[0] = emissive.x;
    mb.emissive[1] = emissive.y;
    mb.emissive[2] = emissive.z;
    mb.ssssWeight = ssssWeight;
    mb.refraction = refraction;
    return mb;
}

// the first five entries are the buffer bindings, in the order vkMakeSet's buffer infos are
// passed (pos, nrm, view UBO, model UBO, uv); the image bindings come after (vkMakeSet only
// writes the buffers, the images are filled lazily by setTexture/writeEnvSet/updateSceneSamplers)
std::vector<VkDescriptorSetLayoutBinding> meshBinds() {
    return {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {14, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {13, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}, // scene depth copy (glass)
        {15, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}, // scene color (glass refraction)
    };
}

std::vector<VkDescriptorSetLayoutBinding> tonemapBinds() {
    return {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    };
}

} // namespace

void Renderer::init(VkApp& app, int nInstances, const Mat4& viewProj) {
    m_dev = app.device();
    m_pdev = app.pdev();
    m_rp = app.renderPass();

    m_camView.viewProj = viewProj; // the first setViewProj overwrites the rest
    vkMakeBuffer(m_dev, m_pdev, m_vpUbuf, m_vpMem, sizeof(ViewUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    vkMakeBuffer(m_dev, m_pdev, m_lightsUbuf, m_lightsMem, sizeof(LightsUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 nullptr);
    VkDescriptorPool pool;
    vkMakeDslPool(m_dev, meshBinds(), (uint32_t)nInstances, m_dsl, pool);
    m_meshPools.push_back(pool);
    m_poolCap = nInstances;
    m_poolUsed = 0;
    // 4 B push constant: the pass id (0 = opaque into sceneColor, 1 = transparent into HDR);
    // the fragment writes the SSS side-channel alpha only in the opaque pass
    m_pl = vkMakePipelineLayoutPC(m_dev, m_dsl, VK_SHADER_STAGE_FRAGMENT_BIT, 4);
    m_pipe = makeGraphicsPipeline(m_dev, app.sceneRenderPass(), mesh_vert_spv, mesh_vert_spv_len / 4, mesh_frag_spv,
                                  mesh_frag_spv_len / 4, m_pl, true, false, false, 2);
    // glass/blend pipeline: alpha blending, depth test without write (the sorted pass over HDR)
    m_pipeBlend = makeGraphicsPipeline(m_dev, app.sceneNoClearRenderPass(), mesh_vert_spv, mesh_vert_spv_len / 4,
                                       mesh_frag_spv, mesh_frag_spv_len / 4, m_pl, true, false, true, 2);
    // glass: back faces culled - with no depth write the two-sided blend order is undefined
    m_pipeBlendCull = makeGraphicsPipeline(m_dev, app.sceneNoClearRenderPass(), mesh_vert_spv, mesh_vert_spv_len / 4,
                                          mesh_frag_spv, mesh_frag_spv_len / 4, m_pl, true, true, true, 2);
    initSss(app);

    const uint8_t white[4] = {255, 255, 255, 255};
    vkMakeImage2D(m_dev, m_pdev, 1, 1, white, m_white.img, m_white.mem, m_white.view);
    const uint8_t neutralNrm[4] = {128, 128, 255, 255}; // tangent-space (0,0,1)
    vkMakeImage2D(m_dev, m_pdev, 1, 1, neutralNrm, m_neutralNrm.img, m_neutralNrm.mem, m_neutralNrm.view);
    vkMakeSamplerEx(m_dev, m_pdev, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, false, 0.0f, 1.0f,
                    m_texSampler);

    vkMakeBuffer(m_dev, m_pdev, m_tmUbuf, m_tmMem, 32, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    const auto tmBinds = tonemapBinds();
    vkMakeDslPool(m_dev, tmBinds, 2, m_tmDsl, m_tmPool);
    m_tmPl = vkMakePipelineLayout(m_dev, m_tmDsl);
    m_tmPipe = makeGraphicsPipeline(m_dev, app.renderPass(), tonemap_vert_spv, tonemap_vert_spv_len / 4,
                                    tonemap_frag_spv, tonemap_frag_spv_len / 4, m_tmPl, false);
    VkAttachmentDescription nca{};
    nca.format = app.swapchainFormat();
    nca.samples = VK_SAMPLE_COUNT_1_BIT;
    nca.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    nca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    nca.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    nca.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    nca.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    nca.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ncr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription ncs{};
    ncs.colorAttachmentCount = 1;
    ncs.pColorAttachments = &ncr;
    VkRenderPassCreateInfo nrpc{};
    nrpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    nrpc.attachmentCount = 1;
    nrpc.pAttachments = &nca;
    nrpc.subpassCount = 1;
    nrpc.pSubpasses = &ncs;
    VK(vkCreateRenderPass(m_dev, &nrpc, nullptr, &m_noClearRp));
    rebuildTonemapSet(app);
    initShadow(app);
}

Renderer::GpuMeshRef Renderer::addMesh(const Mesh& mesh) {
    const auto n = (uint32_t)mesh.vertexCount();
    const VkDeviceSize attrSize = (VkDeviceSize)12 * n;
    GpuMesh g;
    g.vtxCount = n;
    g.idxCount = (uint32_t)mesh.indices.size();
    vkMakeBufferDev(m_dev, m_pdev, g.pos, g.posMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.pos.data());
    vkMakeBufferDev(m_dev, m_pdev, g.nrm, g.nrmMem, attrSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh.nrm.data());
    vkMakeBufferDev(m_dev, m_pdev, g.uv, g.uvMem, (VkDeviceSize)8 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    mesh.uv.data());
    vkMakeBufferDev(m_dev, m_pdev, g.ibuf, g.ibmem, (VkDeviceSize)g.idxCount * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    mesh.indices.data());
    m_geoms.push_back(g);
    return {(int)m_geoms.size() - 1, MeshGpu{VertexStore{g.pos, g.posMem, n}, VertexStore{g.nrm, g.nrmMem, n}}};
}

void Renderer::addSharedMesh(entt::entity geomEnt, const Mesh& m, VkBuffer pos, VkDeviceMemory posMem, VkBuffer nrm,
                             VkDeviceMemory nrmMem, uint32_t n) {
    GpuMesh g;
    g.vtxCount = n;
    g.idxCount = (uint32_t)m.indices.size();
    g.externalPosNrm = true;
    g.pos = pos;
    g.posMem = posMem;
    g.nrm = nrm;
    g.nrmMem = nrmMem;
    vkMakeBufferDev(m_dev, m_pdev, g.uv, g.uvMem, (VkDeviceSize)8 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m.uv.data());
    vkMakeBufferDev(m_dev, m_pdev, g.ibuf, g.ibmem, (VkDeviceSize)g.idxCount * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    m.indices.data());
    m_geoms.push_back(g);
    m_entGeom[geomEnt] = (int)m_geoms.size() - 1;
}

void Renderer::shareGeomSlot(entt::entity geomEnt, int slot) {
    m_entGeom[geomEnt] = slot;
}

// geometry entity -> m_geoms index: an already-uploaded slot, or a first-sight upload of the
// entity's Geometry component (skinned prims get their slot from addSharedMesh at import)
int Renderer::geomFor(entt::entity g, const entt::registry& reg) {
    if (g == entt::null)
        return -1;
    const auto it = m_entGeom.find(g);
    if (it != m_entGeom.end())
        return it->second;
    const auto* gc = reg.try_get<Geometry>(g);
    if (!gc) {
        if (m_geomWarned.insert(g).second) {
            const auto* n = reg.try_get<Name>(g);
            std::fprintf(stderr, "3dsim: geometry entity '%s' has no Geometry component\n",
                         n ? n->id.c_str() : "(unnamed)");
        }
        return -1;
    }
    Mesh m = gc->mesh;
    const int nv = m.vertexCount();
    if (nv <= 0) {
        if (m_geomWarned.insert(g).second)
            std::fprintf(stderr, "3dsim: geometry entity has no vertices\n");
        return -1;
    }
    // normalize: empty nrm/uv pad to zero (the upload needs full-size buffers)
    if (m.nrm.empty())
        m.nrm.assign((size_t)nv * 3, 0.0f);
    if (m.uv.empty())
        m.uv.assign((size_t)nv * 2, 0.0f);
    for (uint32_t i : m.indices)
        if (i >= (uint32_t)nv) {
            if (m_geomWarned.insert(g).second)
                std::fprintf(stderr, "3dsim: geometry entity has an out-of-range index\n");
            return -1;
        }
    m_entGeom[g] = addMesh(m).geom;
    return m_entGeom[g];
}

int Renderer::allocInstance(int geom) {
    const GpuMesh& g = m_geoms[geom];
    InstancedMesh inst;
    inst.geom = geom;
    // prepBuffers rewrites this from the ECS every frame; identity + defaults just keep it valid
    const ModelUbo modelInit = packModel(mat4Identity(), V3{1.0f, 1.0f, 1.0f}, 0.0f, 0.5f, V3{0.0f, 0.0f, 0.0f}, 1.0f,
                                        0.5f, 0.0f, 0.0f, 0.0f);
    vkMakeBuffer(m_dev, m_pdev, inst.modelUbuf, inst.modelMem, sizeof(ModelUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 &modelInit);
    const VkDeviceSize attrSize = (VkDeviceSize)12 * g.vtxCount;
    std::vector<VkDescriptorBufferInfo> bi = {{g.pos, 0, attrSize},
                                              {g.nrm, 0, attrSize},
                                              {m_vpUbuf, 0, sizeof(ViewUbo)},
                                              {inst.modelUbuf, 0, sizeof(ModelUbo)},
                                              {g.uv, 0, (VkDeviceSize)8 * g.vtxCount}};
    if (m_poolUsed + 1 >= m_poolCap) {
        m_meshPools.push_back(vkMakePool(m_dev, meshBinds(), (uint32_t)kPoolChunk));
        m_poolCap = kPoolChunk;
        m_poolUsed = 0;
    }
    vkMakeSet(m_dev, m_meshPools.back(), m_dsl, meshBinds(), inst.set, bi);
    vkMakeSet(m_dev, m_meshPools.back(), m_dsl, meshBinds(), inst.setTrans, bi);
    m_poolUsed += 2;
    m_insts.push_back(inst);
    const int id = (int)m_insts.size() - 1;
    for (int c = 0; c < kTexChannels; ++c)
        setTexture(id, c, -1);
    VkDescriptorBufferInfo lb{m_lightsUbuf, 0, sizeof(LightsUbo)};
    VkWriteDescriptorSet sw[4]{};
    for (int i = 0; i < 4; ++i) {
        sw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sw[i].dstSet = (i & 1) ? inst.setTrans : inst.set;
        sw[i].dstBinding = (i < 2) ? 12 : 13;
        sw[i].descriptorCount = 1;
        sw[i].descriptorType = (i < 2) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        if (i < 2)
            sw[i].pImageInfo = &m_shadowInfo;
        else
            sw[i].pBufferInfo = &lb;
    }
    vkUpdateDescriptorSets(m_dev, 4, sw, 0, nullptr);
    if (m_envReady) {
        writeEnvSet(inst.set);
        writeEnvSet(inst.setTrans);
    }
    m_samplersDirty = true; // its sceneColor/depthCopy bindings need the next sampler update
    return id;
}

void Renderer::addTexture(entt::entity e, const void* rgba, uint32_t w, uint32_t h) {
    GpuTexture t;
    vkMakeImage2D(m_dev, m_pdev, w, h, rgba, t.img, t.mem, t.view);
    m_texs.push_back(t);
    m_entTex[e] = (int)m_texs.size() - 1;
}

int Renderer::instFor(entt::entity e, entt::entity geomEnt, const entt::registry& reg) {
    const int geom = geomFor(geomEnt, reg);
    if (geom < 0)
        return -1;
    auto it = m_entInst.find(e);
    if (it != m_entInst.end() && !m_insts[(size_t)it->second].dead && m_insts[(size_t)it->second].geom == geom)
        return it->second;
    if (it != m_entInst.end())
        queueFreeInst(it->second); // the geom changed: replace the slot
    const int inst = allocInstance(geom);
    m_entInst[e] = inst;
    return inst;
}

int Renderer::instOf(entt::entity e) const {
    const auto it = m_entInst.find(e);
    return it != m_entInst.end() ? it->second : -1;
}

int Renderer::textureId(entt::entity e) const {
    const auto it = m_entTex.find(e);
    return it != m_entTex.end() ? it->second : -1;
}

// the texture id for an entity: an already-uploaded slot, or a first-sight upload of its
// Texture{path} file (glTF embedded textures are uploaded eagerly at import and hit the map)
int Renderer::textureFor(entt::entity e, const entt::registry& reg) {
    if (e == entt::null)
        return -1;
    const int id = textureId(e);
    if (id >= 0)
        return id;
    const auto* t = reg.try_get<Texture>(e);
    if (!t)
        return -1;
    TextureData td;
    bool ok = loadTexture(exeDir() + "/../assets/" + t->path, td);
    if (!ok)
        ok = loadTexture("assets/" + t->path, td);
    if (!ok || td.rgba.empty()) {
        if (m_texWarned.insert(e).second)
            std::fprintf(stderr, "3dsim: texture '%s' not found\n", t->path.c_str());
        return -1;
    }
    addTexture(e, td.rgba.data(), (uint32_t)td.w, (uint32_t)td.h);
    return textureId(e);
}

void Renderer::queueFreeInst(int inst) {
    m_insts[(size_t)inst].dead = true;
    m_freeInst.push_back(inst);
}

// the ECS is the source of truth: a component destroy (or mass destroy) pushes the free back;
// GPU resources wait for the next frame start (the previous frame may still read them). The
// descriptor set is pool-owned and stays allocated (its stale bindings are never drawn)
void Renderer::onRenderableDestroyed(entt::registry&, entt::entity e) {
    const auto it = m_entInst.find(e);
    if (it != m_entInst.end()) {
        queueFreeInst(it->second);
        m_entInst.erase(it);
    }
    // skinned prims are their own geometry entity (no Geometry component to watch)
    m_entGeom.erase(e);
}

void Renderer::onTextureDestroyed(entt::registry&, entt::entity e) {
    // a texture may still be bound by a live instance's descriptor; content destroys a texture
    // only after the materials/meshes that use it
    const auto it = m_entTex.find(e);
    if (it != m_entTex.end()) {
        m_freeTex.push_back(it->second);
        m_entTex.erase(it);
    }
}

// the geometry entity is gone: drop the mapping (the GpuMesh slot's buffers are never freed
// today, matching the pre-existing mesh-table behavior)
void Renderer::onGeometryDestroyed(entt::registry&, entt::entity e) {
    m_entGeom.erase(e);
}

void Renderer::watch(entt::registry& reg) {
    reg.on_destroy<Renderable>().connect<&Renderer::onRenderableDestroyed>(this);
    reg.on_destroy<Texture>().connect<&Renderer::onTextureDestroyed>(this);
    reg.on_destroy<Geometry>().connect<&Renderer::onGeometryDestroyed>(this);
}

void Renderer::flushFrees() {
    for (const int inst : m_freeInst) {
        InstancedMesh& im = m_insts[(size_t)inst];
        vkFreeBuffer(m_dev, im.modelUbuf, im.modelMem);
        im.modelUbuf = VK_NULL_HANDLE;
        im.modelMem = VK_NULL_HANDLE;
    }
    m_freeInst.clear();
    for (const int id : m_freeTex) {
        GpuTexture& t = m_texs[(size_t)id];
        vkFreeImage2D(m_dev, t.img, t.mem, t.view);
        t.img = VK_NULL_HANDLE;
        t.mem = VK_NULL_HANDLE;
        t.view = VK_NULL_HANDLE;
    }
    m_freeTex.clear();
}

void Renderer::setTexture(int inst, int channel, int texId) {
    static const int kBinding[kTexChannels] = {6, 7, 11, 14}; // albedo, metallicRoughness, normal, emissive
    InstancedMesh& im = m_insts[inst];
    if (im.tex[channel] == texId)
        return;
    im.tex[channel] = texId;
    const bool hasTex = texId >= 0 && (size_t)texId < m_texs.size();
    const GpuTexture& t = hasTex ? m_texs[texId] : (channel == 2 ? m_neutralNrm : m_white);
    VkDescriptorImageInfo ii;
    ii.sampler = m_texSampler;
    ii.imageView = t.view;
    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet w[2]{};
    for (int i = 0; i < 2; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = (i == 0) ? im.set : im.setTrans;
        w[i].dstBinding = kBinding[channel];
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &ii;
    }
    vkUpdateDescriptorSets(m_dev, 2, w, 0, nullptr);
}

void Renderer::reset() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    flushFrees(); // already-flushed slots hold null handles (a no-op free)
    for (auto& inst : m_insts)
        vkFreeBuffer(m_dev, inst.modelUbuf, inst.modelMem);
    m_insts.clear();
    m_freeInst.clear();
    m_entInst.clear();
    for (auto& t : m_texs)
        vkFreeImage2D(m_dev, t.img, t.mem, t.view);
    m_texs.clear();
    m_freeTex.clear();
    m_entTex.clear();
    for (auto& g : m_geoms) {
        if (!g.externalPosNrm) {
            vkFreeBuffer(m_dev, g.pos, g.posMem);
            vkFreeBuffer(m_dev, g.nrm, g.nrmMem);
        }
        vkFreeBuffer(m_dev, g.uv, g.uvMem);
        vkFreeBuffer(m_dev, g.ibuf, g.ibmem);
    }
    m_geoms.clear();
    m_entGeom.clear();
    m_geomWarned.clear();
    m_texWarned.clear();
    // the pools persist (their stale sets are never drawn); the next alloc grows a fresh pool
    m_poolCap = 0;
    m_poolUsed = 0;
    releaseEnv();
}

void Renderer::setViewProj(const CameraView& cv) {
    m_camView = cv;
}

void Renderer::writeViewUbo(const CameraView& cv, float envIntensity, VkExtent2D ext) {
    ViewUbo u{};
    std::memcpy(u.viewProj, cv.viewProj.m, 64);
    std::memcpy(u.view, cv.view.m, 64);
    u.camPos[0] = cv.pos.x;
    u.camPos[1] = cv.pos.y;
    u.camPos[2] = cv.pos.z;
    u.envIntensity = envIntensity;
    u.shadowTexels = (float)m_shadowSize;
    u.frustum[0] = cv.tanL;
    u.frustum[1] = cv.tanR;
    u.frustum[2] = cv.tanU;
    u.frustum[3] = cv.tanD;
    u.zNear = cv.zNear;
    u.zFar = cv.zFar;
    u.screenSize[0] = (float)ext.width;
    u.screenSize[1] = (float)ext.height;
    vkWriteBuffer(m_dev, m_vpMem, &u, sizeof(ViewUbo));
}

void Renderer::prepBuffers(const entt::registry& reg) {
    writeLights(reg);
    m_hasSss = false;
    m_hasGlass = false;
    m_hasTrans = false;
    for (auto [e, rend] : reg.view<Renderable>().each()) {
        const int inst = instFor(e, rend.geom, reg); // first sight: upload the geometry + slot
        if (inst < 0)
            continue; // the geometry is missing (warned once at the upload)
        InstancedMesh& im = m_insts[(size_t)inst];
        V3 base{1.0f, 1.0f, 1.0f}, emissive{0.0f, 0.0f, 0.0f};
        float metallic = 0.0f, roughness = 0.5f, baseAlpha = 1.0f, alphaCutoff = 0.5f;
        float ssssWeight = 0.0f, refraction = 0.0f;
        int alphaMode = 0;
        if (const auto* ref = reg.try_get<MaterialRef>(e))
            if (const auto* mat = reg.try_get<Material>(ref->mat)) {
                base = mat->baseColor;
                emissive = mat->emissive;
                metallic = mat->metallic;
                roughness = mat->roughness;
                baseAlpha = mat->alpha;
                alphaCutoff = mat->alphaCutoff;
                alphaMode = mat->alphaMode;
                ssssWeight = mat->ssssWeight;
                refraction = mat->refraction;
                const entt::entity slots[kTexChannels] = {mat->tex, mat->texMR, mat->texNormal, mat->texEmissive};
                for (int c = 0; c < kTexChannels; ++c)
                    setTexture(inst, c, textureFor(slots[c], reg)); // no-ops when the id is unchanged
            }
        // soft bodies resolve no WorldTransform (geometry is world-space in the GPU buffer) -> identity
        Mat4 model = mat4Identity();
        if (const auto* wt = reg.try_get<WorldTransform>(e)) {
            model = wt->toMat4();
            im.wpos = wt->pos;
        }
        im.alphaMode = alphaMode;
        im.refraction = refraction;
        im.visible = rend.visible;
        if (ssssWeight > 0.0f)
            m_hasSss = true;
        if (refraction > 0.0f)
            m_hasGlass = true;
        if (alphaMode == 2 || refraction > 0.0f)
            m_hasTrans = true;
        const ModelUbo mb = packModel(model, base, metallic, roughness, emissive, baseAlpha, alphaCutoff, alphaMode,
                                      ssssWeight, refraction);
        vkWriteBuffer(m_dev, im.modelMem, &mb, sizeof(ModelUbo));
    }
}

namespace {

// ALL_GRAPHICS stages keep the layout/access tracking simple and correct (the passes are short)
void imgBarrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlagBits aspect, VkImageLayout oldL, VkImageLayout newL,
                VkAccessFlags srcAcc, VkAccessFlags dstAcc, uint32_t mips, uint32_t baseLevel = 0) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.image = img;
    b.subresourceRange = {aspect, baseLevel, mips, 0, 1};
    b.oldLayout = oldL;
    b.newLayout = newL;
    b.srcAccessMask = srcAcc;
    b.dstAccessMask = dstAcc;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
}

// push the pass id (0 = opaque, 1 = transparent) once per pass
void setPassId(VkCommandBuffer cmd, VkPipelineLayout pl, float id) {
    vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &id);
}

float halfToFloat(uint16_t h) {
    const uint32_t sign = (uint32_t)(h & 0x8000) << 16;
    const uint32_t exp = (h >> 10) & 0x1F;
    const uint32_t mant = h & 0x3FF;
    uint32_t out = sign;
    if (exp > 0 && exp < 31)
        out |= ((exp + 112) << 23) | (mant << 13);
    else if (exp == 31)
        out |= 0x7F << 23;
    float f;
    std::memcpy(&f, &out, 4);
    return f;
}

} // namespace

// the opaque scene into sceneColor (A = the ssssWeight side channel; the background clears to
// (bg, 0) so the SSS blur never bleeds into it). Glass (refraction > 0) draws in the sorted
// pass, like the blend objects (its self-composition overwrites the blitted scene)
void Renderer::renderOpaquePass(VkCommandBuffer cmd, VkApp& app, const float bg[3]) {
    const VkExtent2D ext = app.extent();
    VkViewport vpt{0, (float)ext.height, (float)ext.width, -(float)ext.height, 0, 1};
    VkRect2D sc{0, 0, ext.width, ext.height};
    VkClearValue scv[3]{};
    scv[0].color = {bg[0], bg[1], bg[2], 0.0f};
    scv[1].depthStencil = {1.0f, 0};
    scv[2].color = {0.0f, 0.0f, 0.0f, 0.0f}; // the diffuse attachment
    VkRenderPassBeginInfo spb{};
    spb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    spb.renderPass = app.sceneRenderPass();
    spb.framebuffer = app.sceneFramebuffer();
    spb.renderArea = {{0, 0}, ext};
    spb.clearValueCount = 3;
    spb.pClearValues = scv;
    vkCmdBeginRenderPass(cmd, &spb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &vpt);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipe);
    setPassId(cmd, m_pl, 0.0f);
    for (const auto& im : m_insts) {
        if (!im.visible || im.dead || im.alphaMode == 2 || im.refraction > 0.0f)
            continue;
        drawInstance(cmd, m_pl, m_geoms[im.geom], im.set);
    }
    vkCmdEndRenderPass(cmd);
}

// the sorted pass (glass + blend objects) over the blitted scene in HDR: back-to-front by
// camera distance, depth test without write; the fragment refraction reads sceneColor + the
// depth copy (mip level = rough * 8)
void Renderer::renderTransparentPass(VkCommandBuffer cmd, VkApp& app, const V3& camPos) {
    if (!m_hasTrans)
        return;
    const VkExtent2D ext = app.extent();
    VkViewport vpt{0, (float)ext.height, (float)ext.width, -(float)ext.height, 0, 1};
    VkRect2D sc{0, 0, ext.width, ext.height};
    m_dbgReadScene = std::getenv("VKSIM_DBG_SCENE") != nullptr;
    VkRenderPass rp = app.sceneNoClearRenderPass();
    VkFramebuffer fb = app.sceneNoClearFramebuffer();
    float passId = 1.0f;
    VkRenderPassBeginInfo spb{};
    spb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    spb.renderPass = rp;
    spb.framebuffer = fb;
    spb.renderArea = {{0, 0}, ext};
    vkCmdBeginRenderPass(cmd, &spb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &vpt);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    std::vector<int> trans;
    for (size_t i = 0; i < m_insts.size(); ++i)
        if (m_insts[i].visible && !m_insts[i].dead &&
            (m_insts[i].alphaMode == 2 || m_insts[i].refraction > 0.0f))
            trans.push_back((int)i);
    std::sort(trans.begin(), trans.end(), [&](int a, int b) {
        const V3 da = vSub(camPos, m_insts[a].wpos), db = vSub(camPos, m_insts[b].wpos);
        return vDot(db, db) > vDot(da, da);
    });
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeBlend);
    setPassId(cmd, m_pl, passId);
    for (int i : trans) {
        // glass: cull the back face (uncull + no depth write makes the blend order undefined)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_insts[i].refraction > 0.0f ? m_pipeBlendCull : m_pipeBlend);
        drawInstance(cmd, m_pl, m_geoms[m_insts[i].geom], m_insts[i].setTrans);
    }
    vkCmdEndRenderPass(cmd);
}

// the Godot-style SSS: two separable compute passes (horizontal sceneColor -> temp, vertical
// temp -> sceneColor) + the depth copy the glass refraction samples (its own buffer: the depth
// is also the transparent pass's depth-stencil attachment, and the layouts are exclusive)
void Renderer::renderSssBlur(VkCommandBuffer cmd, VkApp& app, const CameraView& cv) {
    const VkExtent2D ext = app.extent();
    if (m_hasGlass || m_hasSss) {
        // the glass (and the sss depth read) sample the R32 copy while the D32 depth is the
        // transparent pass's depth-stencil attachment (exclusive layouts): a direct
        // format-converting copy (a depth sampler would only yield the 0/1 compare result)
        VkImageCopy ic{};
        ic.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        ic.srcOffset = {0, 0, 0};
        ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        ic.dstOffset = {0, 0, 0};
        ic.extent = {(uint32_t)ext.width, (uint32_t)ext.height, 1};
        vkCmdCopyImage(cmd, app.depthImage(), VK_IMAGE_LAYOUT_GENERAL, app.depthCopyImage(),
                       VK_IMAGE_LAYOUT_GENERAL, 1, &ic);
        // the sss pass samples the copy right after the transfer wrote it (the descriptor's
        // layout is SHADER_READ_ONLY, so the copy must leave the GENERAL layout)
        if (m_hasSss)
            imgBarrier(cmd, app.depthCopyImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT, 1);
    }
    if (!m_hasSss)
        return;
    SssPc pc{};
    pc.screenSize[0] = (float)ext.width;
    pc.screenSize[1] = (float)ext.height;
    pc.zNear = cv.zNear;
    pc.zFar = cv.zFar;
    pc.unitSize = unitSizeOf(cv);
    pc.scale = m_sssScale;
    pc.depthScale = m_sssDepthScale;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_sss.pipe);
    for (int pass = 0; pass < 2; ++pass) {
        pc.vertical = pass == 1 ? 1.0f : 0.0f;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_sss.pl, 0, 1, &m_sssSet[pass], 0, nullptr);
        vkCmdPushConstants(cmd, m_sss.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SssPc), &pc);
        vkCmdDispatch(cmd, (ext.width + 7) / 8, (ext.height + 7) / 8, 1);
        if (pass == 0)
            imgBarrier(cmd, app.sceneColorTempImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
    }
}

// the full per-eye sequence: opaque -> [depth copy + SSS] -> [mips] -> blit to HDR ->
// transparent (glass). The tonemap (and the caller's other work) comes after
void Renderer::renderSceneBuffers(VkCommandBuffer cmd, VkApp& app, const float bg[3], const CameraView& cv) {
    const VkExtent2D ext = app.extent();
    const uint32_t mips = app.sceneColorMips();
    renderOpaquePass(cmd, app, bg);
    const bool needRead = m_hasSss || m_hasGlass; // the depth + sceneColor get sampled this frame
    if (needRead) {
        // only mip 0 came out of the opaque pass (COLOR_ATTACHMENT_OPTIMAL); the other mips
        // are still UNDEFINED and must not be claimed from an attachment layout. the sss pass
        // treats sceneColor as a storage image (GENERAL); the glass samples it (SHADER_READ)
        const VkImageLayout scNew = m_hasSss ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const VkAccessFlags scDst = m_hasSss ? (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
                                            : VK_ACCESS_SHADER_READ_BIT;
        imgBarrier(cmd, app.sceneColorImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   scNew, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, scDst, 1);
        imgBarrier(cmd, app.depthImage(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL,
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT, 1);
        if (m_hasSss) {
            imgBarrier(cmd, app.sceneColorTempImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, 1);
            // the sss storage descriptors (sceneColor, diffuse) identify the full mip chain; mips
            // 1..N-1 must be in the written layout (GENERAL) even when the glass pass (the only
            // one that regenerates them) does not run
            if (mips > 1)
                imgBarrier(cmd, app.sceneColorImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_READ_BIT, mips - 1, 1);
            // the diffuse is sampled (pass 0) and imageLoad'd (pass 1); never written
            imgBarrier(cmd, app.sssDiffuseImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_READ_BIT, 1);
        }
        // the sss pass (and the glass pass) sample the depth copy
        if (needRead)
            imgBarrier(cmd, app.depthCopyImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, 1);
    }
    renderSssBlur(cmd, app, cv);
    // sceneColor -> GENERAL for the mip generation (glass roughness blur) + the HDR blit
    const VkImageLayout sceneOld =
        m_hasSss ? VK_IMAGE_LAYOUT_GENERAL : needRead ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                     : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    const VkAccessFlags sceneSrc =
        m_hasSss ? VK_ACCESS_SHADER_WRITE_BIT : needRead ? VK_ACCESS_SHADER_READ_BIT
                                                        : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imgBarrier(cmd, app.sceneColorImage(), VK_IMAGE_ASPECT_COLOR_BIT, sceneOld, VK_IMAGE_LAYOUT_GENERAL, sceneSrc,
               VK_ACCESS_TRANSFER_READ_BIT | (m_hasGlass ? VK_ACCESS_TRANSFER_WRITE_BIT : 0), 1);
    if (m_hasGlass && mips > 1)
        // when sss ran, needRead already moved the mips to GENERAL
        imgBarrier(cmd, app.sceneColorImage(), VK_IMAGE_ASPECT_COLOR_BIT,
                   m_hasSss ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, mips - 1,
                   1);
    if (m_hasGlass)
        for (uint32_t mip = 1; mip < mips; ++mip) {
            // image mip dims are clamped to 1: the shifts must be too (zero-extent is invalid)
            const int sw = std::max(1, (int)(ext.width >> (mip - 1)));
            const int sh = std::max(1, (int)(ext.height >> (mip - 1)));
            const int dw = std::max(1, (int)(ext.width >> mip));
            const int dh = std::max(1, (int)(ext.height >> mip));
            VkImageBlit ib{};
            ib.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
            ib.srcOffsets[1] = {sw, sh, 1};
            ib.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            ib.dstOffsets[1] = {dw, dh, 1};
            vkCmdBlitImage(cmd, app.sceneColorImage(), VK_IMAGE_LAYOUT_GENERAL, app.sceneColorImage(),
                           VK_IMAGE_LAYOUT_GENERAL, 1, &ib, VK_FILTER_LINEAR);
        }
    if (app.hdrView() != m_hdrView) {
        m_hdrView = app.hdrView();
        m_hdrFirst = true; // a fresh hdr image starts in UNDEFINED
    }
    const VkImageLayout hdrOld =
        m_hdrFirst ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // after the tonemap
    const VkAccessFlags hdrSrcA = m_hdrFirst ? 0 : VK_ACCESS_SHADER_READ_BIT;
    imgBarrier(cmd, app.hdrImage(), VK_IMAGE_ASPECT_COLOR_BIT, hdrOld, VK_IMAGE_LAYOUT_GENERAL, hdrSrcA,
               VK_ACCESS_TRANSFER_WRITE_BIT, 1);
    {
        VkImageBlit ib{};
        ib.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        ib.srcOffsets[1] = {int(ext.width), int(ext.height), 1};
        ib.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        ib.dstOffsets[1] = {int(ext.width), int(ext.height), 1};
        vkCmdBlitImage(cmd, app.sceneColorImage(), VK_IMAGE_LAYOUT_GENERAL, app.hdrImage(), VK_IMAGE_LAYOUT_GENERAL,
                       1, &ib, VK_FILTER_LINEAR);
    }
    // CA_OPTIMAL: the transparent pass's attachment initial layout (runs or not)
    imgBarrier(cmd, app.hdrImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
    m_hdrFirst = false;
    if (needRead)
        imgBarrier(cmd, app.depthImage(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, 1);
    if (m_hasGlass) {
        // the sss pass already left the copy in SHADER_READ_ONLY (layout + access); the glass
        // pass (which runs after it) needs no further transition in that case
        imgBarrier(cmd, app.depthCopyImage(), VK_IMAGE_ASPECT_COLOR_BIT,
                   m_hasSss ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   m_hasSss ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                   VK_ACCESS_SHADER_READ_BIT, 1);
        // sceneColor is in GENERAL after the mip / HDR blits; the glass pass samples it as
        // SHADER_READ_ONLY, so transition it back before the transparent pass
        imgBarrier(cmd, app.sceneColorImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                   VK_ACCESS_SHADER_READ_BIT, mips);
    }
    renderTransparentPass(cmd, app, cv.pos);
}

// update the view-shared scene samplers before any pass binds the instance sets
// (updating a set bound to the command buffer invalidates the buffer)
void Renderer::ensureSceneSamplers(VkApp& app) {
    if (app.sceneColorView() != m_sssSceneView || app.sceneColorTempView() != m_sssTempView ||
        app.depthView() != m_sssDepthView || app.depthCopyView() != m_sssDepthCopyView ||
        app.sssDiffuseView() != m_sssDiffuseView ||
        m_samplersDirty) {
        updateSceneSamplers(app);
        m_samplersDirty = false;
    }
}

void Renderer::updateSceneSamplers(VkApp& app) {
    m_sssSceneView = app.sceneColorView();
    m_sssTempView = app.sceneColorTempView();
    m_sssDepthView = app.depthView();
    m_sssDepthCopyView = app.depthCopyView();
    m_sssDiffuseView = app.sssDiffuseView();
    const VkDescriptorImageInfo sceneSam{m_sceneColorSampler, app.sceneColorView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo sceneStore{VK_NULL_HANDLE, app.sceneColorView(), VK_IMAGE_LAYOUT_GENERAL};
    // the sss passes only touch these in GENERAL (pass 1 loads + stores sceneColor)
    const VkDescriptorImageInfo sceneRw{VK_NULL_HANDLE, app.sceneColorView(), VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo diffSam{m_sceneColorSampler, app.sssDiffuseView(), VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo diffRw{VK_NULL_HANDLE, app.sssDiffuseView(), VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo tempSam{m_sceneColorSampler, app.sceneColorTempView(),
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo tempStore{VK_NULL_HANDLE, app.sceneColorTempView(), VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo dcopySam{m_depthCopySampler, app.depthCopyView(),
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // SSS set 0: diffuse (sampler) -> temp (store) + depth copy; set 1: temp (sampler) -> sceneColor (store)
    // set 0/1 both carry sceneColor + diffuse as read/write storage (the vertical pass composites)
    VkWriteDescriptorSet w[10]{};
    for (auto& entry : w) {
        entry.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        entry.descriptorCount = 1;
    }
    w[0].dstSet = m_sssSet[0];
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &diffSam;
    w[1].dstSet = m_sssSet[0];
    w[1].dstBinding = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &tempStore;
    w[2].dstSet = m_sssSet[0];
    w[2].dstBinding = 2;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[2].pImageInfo = &dcopySam;
    w[3].dstSet = m_sssSet[1];
    w[3].dstBinding = 0;
    w[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[3].pImageInfo = &tempSam;
    w[4].dstSet = m_sssSet[1];
    w[4].dstBinding = 1;
    w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[4].pImageInfo = &sceneStore;
    w[5].dstSet = m_sssSet[1];
    w[5].dstBinding = 2;
    w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[5].pImageInfo = &dcopySam;
    w[6].dstSet = m_sssSet[0];
    w[6].dstBinding = 3;
    w[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[6].pImageInfo = &sceneRw;
    w[7].dstSet = m_sssSet[0];
    w[7].dstBinding = 4;
    w[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[7].pImageInfo = &diffRw;
    w[8].dstSet = m_sssSet[1];
    w[8].dstBinding = 3;
    w[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[8].pImageInfo = &sceneRw;
    w[9].dstSet = m_sssSet[1];
    w[9].dstBinding = 4;
    w[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[9].pImageInfo = &diffRw;
    vkUpdateDescriptorSets(m_dev, 10, w, 0, nullptr);
    // opaque set: 2/15 hold a neutral (sceneColor is the opaque pass's own attachment);
    // trans set: the real scene samplers (the glass refraction)
    const VkDescriptorImageInfo neutral{m_texSampler, m_white.view, VK_IMAGE_LAYOUT_GENERAL};
    std::vector<VkWriteDescriptorSet> wi;
    for (size_t i = 0; i < m_insts.size(); ++i) {
        if (m_insts[i].dead)
            continue;
        const VkDescriptorImageInfo* ti[2] = {&dcopySam, &sceneSam};
        const uint32_t binds[2] = {2, 15};
        for (int b = 0; b < 2; ++b) {
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = m_insts[i].set;
            w.dstBinding = binds[b];
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &neutral;
            wi.push_back(w);
            w.dstSet = m_insts[i].setTrans;
            w.pImageInfo = ti[b];
            wi.push_back(w);
        }
    }
    if (wi.size())
        vkUpdateDescriptorSets(m_dev, (uint32_t)wi.size(), wi.data(), 0, nullptr);
}

void Renderer::initSss(VkApp& app) {
    const std::vector<VkDescriptorSetLayoutBinding> sssBinds = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
    };
    vkMakeDslPool(m_dev, sssBinds, 2, m_sss.dsl, m_sss.pool);
    m_sss.pl = vkMakePipelineLayoutPC(m_dev, m_sss.dsl, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(SssPc));
    {
        VkShaderModule cm = vkMakeModule(m_dev, sss_blur_spv, sss_blur_spv_len / 4);
        VkPipelineShaderStageCreateInfo cs{};
        cs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cs.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cs.module = cm;
        cs.pName = "main";
        VkComputePipelineCreateInfo cpc{};
        cpc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpc.stage = cs;
        cpc.layout = m_sss.pl;
        VK(vkCreateComputePipelines(m_dev, VK_NULL_HANDLE, 1, &cpc, nullptr, &m_sss.pipe));
        vkDestroyShaderModule(m_dev, cm, nullptr);
    }
    VkDescriptorSetLayout sssLayouts[2] = {m_sss.dsl, m_sss.dsl};
    VkDescriptorSetAllocateInfo sa{};
    sa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sa.descriptorPool = m_sss.pool;
    sa.descriptorSetCount = 2;
    sa.pSetLayouts = sssLayouts;
    VK(vkAllocateDescriptorSets(m_dev, &sa, m_sssSet));
    vkMakeSamplerEx(m_dev, m_pdev, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, true,
                    (float)app.sceneColorMips(), 1000.0f, m_sceneColorSampler);
    vkMakeSamplerEx(m_dev, m_pdev, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 0.0f,
                    1000.0f, m_depthCopySampler);
    updateSceneSamplers(app);
}

void Renderer::releaseSss() {
    if (m_sss.pool)
        vkDestroyDescriptorPool(m_dev, m_sss.pool, nullptr);
    if (m_sss.dsl)
        vkDestroyDescriptorSetLayout(m_dev, m_sss.dsl, nullptr);
    if (m_sss.pl)
        vkDestroyPipelineLayout(m_dev, m_sss.pl, nullptr);
    if (m_sss.pipe)
        vkDestroyPipeline(m_dev, m_sss.pipe, nullptr);
    m_sssSet[0] = m_sssSet[1] = VK_NULL_HANDLE;
    for (VkSampler s : {m_sceneColorSampler, m_depthCopySampler})
        if (s)
            vkDestroySampler(m_dev, s, nullptr);
    m_sceneColorSampler = m_depthCopySampler = VK_NULL_HANDLE;
}

void Renderer::hdrBarrier(VkCommandBuffer cmd, VkApp& app) {
    VkImageMemoryBarrier imb{};
    imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.image = app.hdrImage();
    imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &imb);
}

void Renderer::writeTonemapUbo(float ox, float oy, float sx, float sy) {
    const float tm[8] = {ox, oy, sx, sy, m_exposure, 0.0f, 0.0f, 0.0f};
    vkWriteBuffer(m_dev, m_tmMem, tm, 32);
}

// tonemap the HDR buffer into a region of the swapchain image. clear = the first eye gets the
// clearing pass/framebuffer, later eyes the no-clear ones. No EndRenderPass: the caller draws
// imgui inside the same pass, then ends it
void Renderer::drawTonemapRegion(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const VkRect2D& region, bool clear) {
    const VkExtent2D ext = app.extent();
    VkClearValue bcv{}; // black
    VkRenderPassBeginInfo bpb{};
    bpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bpb.renderPass = clear ? app.renderPass() : m_noClearRp;
    bpb.framebuffer = clear ? app.frameBuffer(fb) : m_noClearFbs[fb];
    bpb.renderArea = {{0, 0}, ext};
    bpb.clearValueCount = 1;
    bpb.pClearValues = &bcv;
    vkCmdBeginRenderPass(cmd, &bpb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vpt{0, (float)ext.height, (float)ext.width, -(float)ext.height, 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vpt);
    vkCmdSetScissor(cmd, 0, 1, &region);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tmPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tmPl, 0, 1, &m_tmSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void Renderer::writeLights(const entt::registry& reg) {
    LightsUbo u{};
    int n = 0;
    reg.view<WorldTransform, PointLight>().each([&](auto, const WorldTransform& wt, const PointLight& p) {
        if (n >= kMaxLights)
            return;
        LightGPU& g = u.lights[n++];
        g.pos[0] = wt.pos.x;
        g.pos[1] = wt.pos.y;
        g.pos[2] = wt.pos.z;
        g.intensity = p.intensity;
        g.color[0] = p.color.x;
        g.color[1] = p.color.y;
        g.color[2] = p.color.z;
        g.radius = p.radius;
        g.on = p.on;
        g.shadowNear = p.shadowNear;
        g.shadowFar = p.shadowFar;
        g.shadowNormalBias = p.shadowNormalBias;
        g.biasBase = p.shadowBiasBase;
        g.biasSlope = p.shadowBiasSlope;
        g.searchScale = p.shadowSearchScale;
        g.maxRadius = p.shadowMaxRadius;
    });
    u.count = n;
    vkWriteBuffer(m_dev, m_lightsMem, &u, sizeof(LightsUbo));
}

void Renderer::drawInstance(VkCommandBuffer cmd, VkPipelineLayout pl, const GpuMesh& g, const VkDescriptorSet set) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &set, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, g.ibuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, g.idxCount, 1, 0, 0, 0);
}

void Renderer::rebuildTonemapSet(VkApp& app) {
    vkResetDescriptorPool(m_dev, m_tmPool, 0);
    VkDescriptorSet sets[2]{};
    const uint32_t n = m_xrReady ? 2 : 1;
    VkDescriptorSetLayout layouts[2] = {m_tmDsl, m_tmDsl}; // pSetLayouts must have n entries
    VkDescriptorSetAllocateInfo sai{};
    sai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sai.descriptorPool = m_tmPool;
    sai.descriptorSetCount = n;
    sai.pSetLayouts = layouts;
    VK(vkAllocateDescriptorSets(m_dev, &sai, sets));
    m_tmSet = sets[0];
    m_xrSet = m_xrReady ? sets[1] : VK_NULL_HANDLE;
    VkDescriptorImageInfo ii;
    ii.sampler = app.hdrSampler();
    ii.imageView = app.hdrView();
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo bi{m_tmUbuf, 0, 32};
    VkDescriptorBufferInfo xbi{m_xrTmUbuf, 0, 32};
    VkWriteDescriptorSet w[4]{};
    for (int s = 0; s < 2; ++s) {
        w[2 * s].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[2 * s].dstSet = sets[s];
        w[2 * s].dstBinding = 0;
        w[2 * s].descriptorCount = 1;
        w[2 * s].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[2 * s].pImageInfo = &ii;
        w[2 * s + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[2 * s + 1].dstSet = sets[s];
        w[2 * s + 1].dstBinding = 1;
        w[2 * s + 1].descriptorCount = 1;
        w[2 * s + 1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[2 * s + 1].pBufferInfo = &bi;
    }
    if (m_xrReady)
        w[3].pBufferInfo = &xbi;
    vkUpdateDescriptorSets(m_dev, 2 * n, w, 0, nullptr);
    m_tmSrcView = app.hdrView();
}

void Renderer::draw(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const entt::registry& reg, const RenderSettings& rs,
                    ImDrawData* imgui) {
    m_exposure = rs.exposure;
    const VkExtent2D ext = app.extent();
    writeViewUbo(m_camView, rs.envIntensity, ext);
    prepBuffers(reg);
    ensureSceneSamplers(app);
    renderShadowCubes(cmd, reg);
    renderSceneBuffers(cmd, app, rs.bgColor, m_camView);
    hdrBarrier(cmd, app);
    if (app.hdrView() != m_tmSrcView)
        rebuildTonemapSet(app);
    writeTonemapUbo(0.0f, 0.0f, (float)ext.width, (float)ext.height);
    drawTonemapRegion(cmd, app, fb, {0, 0, ext.width, ext.height}, true);
    if (imgui && imgui->CmdLists.Size > 0)
        ImGui_ImplVulkan_RenderDrawData(imgui, cmd);
    const bool dbgHdr = std::getenv("VKSIM_DBG_HDR") != nullptr || std::getenv("VKSIM_DBG_SCENE") != nullptr;
    const bool dbgMips = std::getenv("VKSIM_DBG_MIPS") != nullptr;
    if ((dbgHdr || dbgMips) && m_dbgBuf == VK_NULL_HANDLE) {
        m_dbgSize = (uint64_t)ext.width * ext.height * 32 + 16 * 1024 * 1024;
        vkMakeBuffer(m_dev, m_pdev, m_dbgBuf, m_dbgMem, (VkDeviceSize)m_dbgSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     nullptr);
    }
    if (dbgHdr) {
        VkImageMemoryBarrier imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.image = m_dbgReadScene ? app.sceneColorImage() : app.hdrImage();
        imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        imb.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // sceneColor (post-barrier) and hdr are both readable
        imb.newLayout = imb.oldLayout;
        imb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &imb);
        VkBufferImageCopy bic{};
        bic.bufferOffset = 0;
        bic.bufferImageHeight = 1;
        bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        bic.imageOffset = {0, 0, 0};
        bic.imageExtent = {ext.width, ext.height, 1};
        if (m_dbgReadScene)
            vkCmdCopyImageToBuffer(cmd, app.sceneColorImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_dbgBuf, 1,
                                  &bic);
        else
            vkCmdCopyImageToBuffer(cmd, app.hdrImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_dbgBuf, 1, &bic);
    }
    if (dbgMips) { // DBG: full sceneColor mip 0, mip 1, final hdr, depth copy (slot * w*h*8)
        for (int slot = 0; slot < 4; ++slot) {
            const bool hdr = slot == 2, dcp = slot == 3;
            const uint32_t mw = ext.width >> ((hdr || dcp) ? 0 : slot);
            const uint32_t mh = ext.height >> ((hdr || dcp) ? 0 : slot);
            const VkImage src = hdr ? app.hdrImage() : dcp ? app.depthCopyImage() : app.sceneColorImage();
            VkImageMemoryBarrier imb{};
            imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imb.image = src;
            imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, hdr ? 0 : (uint32_t)slot, 1, 0, 1};
            imb.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imb.newLayout = imb.oldLayout;
            imb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                nullptr, 0, nullptr, 1, &imb);
            VkBufferImageCopy bic{};
            bic.bufferOffset = (uint64_t)slot * ext.width * ext.height * 8;
            bic.bufferImageHeight = 1;
            bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, hdr ? 0 : (uint32_t)slot, 0, 1};
            bic.imageOffset = {0, 0, 0};
            bic.imageExtent = {mw, mh, 1};
            vkCmdCopyImageToBuffer(cmd, src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_dbgBuf, 1, &bic);
        }
        if (m_envReady)
            for (uint32_t face = 0; face < 6; ++face) { // env cubemap mip 0 faces at 28*w*h bytes
            VkImageMemoryBarrier imb{};
            imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imb.image = m_envPre.img;
            imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, face, 1};
            imb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            imb.newLayout = imb.oldLayout;
            imb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                nullptr, 0, nullptr, 1, &imb);
            VkBufferImageCopy bic{};
            bic.bufferOffset = (uint64_t)28 * ext.width * ext.height + (uint64_t)face * 256 * 256 * 8;
            bic.bufferImageHeight = 1;
            bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, face, 1};
            bic.imageOffset = {0, 0, 0};
            bic.imageExtent = {256, 256, 1};
            vkCmdCopyImageToBuffer(cmd, m_envPre.img, VK_IMAGE_LAYOUT_GENERAL, m_dbgBuf, 1, &bic);
        }
        if (m_envReady) { // the equirect as uploaded, after the cube faces
            const size_t eqOff = (size_t)28 * ext.width * ext.height + 6 * 256 * 256 * 8;
            VkImageMemoryBarrier imb{};
            imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imb.image = m_envEq.img;
            imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            imb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            imb.newLayout = imb.oldLayout;
            imb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                nullptr, 0, nullptr, 1, &imb);
            VkBufferImageCopy bic{};
            bic.bufferOffset = eqOff;
            bic.bufferImageHeight = 1;
            bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            bic.imageOffset = {0, 0, 0};
            bic.imageExtent = {m_envEqW, m_envEqH, 1};
            vkCmdCopyImageToBuffer(cmd, m_envEq.img, VK_IMAGE_LAYOUT_GENERAL, m_dbgBuf, 1, &bic);
        }
    }
    vkCmdEndRenderPass(cmd);
    app.submit(cmd);
    if (dbgHdr) {
        void* p;
        VK(vkMapMemory(m_dev, m_dbgMem, 0, m_dbgSize, 0, &p));
        const uint16_t* px = (const uint16_t*)p;
        std::fprintf(stderr, "DBG HDR:");
        for (int i = 0; i < 5; ++i) {
            const uint16_t* q = px + ((int)ext.height / 2 * (int)ext.width + (i * (int)ext.width) / 5) * 4;
            std::fprintf(stderr, " (%.3f %.3f %.3f %.3f)", halfToFloat(q[0]), halfToFloat(q[1]), halfToFloat(q[2]),
                         halfToFloat(q[3]));
        }
        std::fprintf(stderr, "\n");
        vkUnmapMemory(m_dev, m_dbgMem);
    }
    if (dbgMips) {
        static bool dbgMipsDone = false;
        if (dbgMipsDone)
            return;
        dbgMipsDone = true;
        void* p;
        VK(vkMapMemory(m_dev, m_dbgMem, 0, m_dbgSize, 0, &p));
        for (int slot = 0; slot < 4; ++slot) {
            const uint32_t mw = ext.width >> ((slot == 2 || slot == 3) ? 0 : slot);
            const uint32_t mh = ext.height >> ((slot == 2 || slot == 3) ? 0 : slot);
            char fn[64];
            std::snprintf(fn, sizeof(fn), "/tmp/scene_%s.pgm", slot == 0 ? "mip0" : slot == 1 ? "mip1" :
                                                                  slot == 2 ? "hdr" : "depth");
            FILE* f = std::fopen(fn, "wb");
            if (slot == 3) { // depth copy is r32f: dump as grayscale
                const float* fp = (const float*)p + (size_t)3 * ext.width * ext.height * 2;
                std::fprintf(f, "P5 %u %u 255\n", mw, mh);
                for (uint32_t y = 0; y < mh; ++y)
                    for (uint32_t x = 0; x < mw; ++x) {
                        float v = fp[(size_t)y * mw + x];
                        std::fputc((int)(std::min(1.0f, v) * 255.0f), f);
                    }
            } else {
                const uint16_t* px = (const uint16_t*)p + (size_t)slot * ext.width * ext.height * 4;
                std::fprintf(f, "P6 %u %u 255\n", mw, mh);
                for (uint32_t y = 0; y < mh; ++y)
                    for (uint32_t x = 0; x < mw; ++x) {
                        const uint16_t* q = px + ((size_t)y * mw + x) * 4;
                        for (int c = 0; c < 3; ++c) {
                            float v = halfToFloat(q[c]);
                            v = v > 1.0f ? 1.0f : v;
                            std::fputc((int)(v * 255.0f), f);
                        }
                    }
            }
            std::fclose(f);
            std::fprintf(stderr, "DBG MIP: wrote %s\n", fn);
        }
        if (m_envReady) { // env cubemap mip 0: 3x2 montage of 256px faces
            const uint16_t* cp = (const uint16_t*)p + (size_t)14 * ext.width * ext.height;
            char fn[64];
            std::snprintf(fn, sizeof(fn), "/tmp/env_cube.pgm");
            FILE* f = std::fopen(fn, "wb");
            std::fprintf(f, "P6 768 512 255\n");
            for (uint32_t face = 0; face < 6; ++face) {
                const uint16_t* fp = cp + (size_t)face * 256 * 256 * 4;
                for (uint32_t y = 0; y < 256; ++y)
                    for (uint32_t x = 0; x < 256; ++x) {
                        const uint16_t* q = fp + ((size_t)y * 256 + x) * 4;
                        for (int c = 0; c < 3; ++c) {
                            float v = halfToFloat(q[c]);
                            v = v > 1.0f ? 1.0f : v;
                            std::fputc((int)(v * 255.0f), f);
                        }
                    }
            }
            std::fclose(f);
            std::fprintf(stderr, "DBG MIP: wrote %s\n", fn);
        }
        if (m_envReady) { // the equirect as uploaded
            const float* ef = (const float*)p + (size_t)7 * ext.width * ext.height + 6 * 256 * 256 * 2;
            char fn[64];
            std::snprintf(fn, sizeof(fn), "/tmp/env_eq.pgm");
            FILE* f = std::fopen(fn, "wb");
            std::fprintf(f, "P6 %u %u 255\n", m_envEqW, m_envEqH);
            for (uint32_t y = 0; y < m_envEqH; ++y)
                for (uint32_t x = 0; x < m_envEqW; ++x) {
                    const float* q = ef + ((size_t)y * m_envEqW + x) * 4;
                    for (int c = 0; c < 3; ++c) {
                        float v = q[c];
                        v = v > 1.0f ? 1.0f : v;
                        std::fputc((int)(v * 255.0f), f);
                    }
                }
            std::fclose(f);
            std::fprintf(stderr, "DBG MIP: wrote %s\n", fn);
        }
        vkUnmapMemory(m_dev, m_dbgMem);
    }
}

Mat4 Renderer::eyeVP(const View& v) const {
    const Mat4 proj = projFov(v.tanL, v.tanR, v.tanU, v.tanD, 0.1f, 300.0f);
    return mul4(proj, viewFromPose(v.pos, v.quat));
}

// letterbox the eye image (its own aspect) into the eye's half of the window
VkRect2D Renderer::eyeRegion(const View& v, VkExtent2D ext, int eye) const {
    const float aspect = (v.tanR - v.tanL) / (v.tanU - v.tanD);
    const uint32_t halfW = ext.width / 2;
    float rw, rh;
    if (aspect > (float)halfW / (float)ext.height) {
        rw = (float)halfW;
        rh = rw / aspect;
    } else {
        rh = (float)ext.height;
        rw = rh * aspect;
    }
    const float ox = eye * (float)halfW + ((float)halfW - rw) * 0.5f;
    const float oy = ((float)ext.height - rh) * 0.5f;
    return {(int)ox, (int)oy, (uint32_t)rw, (uint32_t)rh};
}

void Renderer::ensureNoClearFbs(VkApp& app) {
    const VkExtent2D ext = app.extent();
    if (m_noClearFbs.size() == app.imageCount() && m_noClearExt.width == ext.width && m_noClearExt.height == ext.height)
        return;
    for (auto* fb : m_noClearFbs)
        vkDestroyFramebuffer(m_dev, fb, nullptr);
    m_noClearFbs.clear();
    m_noClearFbs.resize(app.imageCount());
    for (uint32_t i = 0; i < app.imageCount(); ++i) {
        const VkImageView att = app.view(i);
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = m_noClearRp;
        fci.attachmentCount = 1;
        fci.pAttachments = &att;
        fci.width = ext.width;
        fci.height = ext.height;
        fci.layers = 1;
        VK(vkCreateFramebuffer(m_dev, &fci, nullptr, &m_noClearFbs[i]));
    }
    m_noClearExt = ext;
}

void Renderer::drawVr(VkCommandBuffer cmd, VkApp& app, uint32_t fb, const entt::registry& reg, const RenderSettings& rs,
                      ImDrawData* imgui, entt::entity cam, const uint32_t* xrImg, int nXr) {
    m_exposure = rs.exposure;
    prepBuffers(reg);
    const VkExtent2D ext = app.extent();
    ensureSceneSamplers(app);
    renderShadowCubes(cmd, reg);
    ensureNoClearFbs(app);
    VkRect2D full{0, 0, ext.width, ext.height};
    const Camera* c = cam != entt::null ? &reg.get<Camera>(cam) : nullptr;
    const int nViews = c ? c->nViews : 0;
    for (int eye = 0; eye < nViews; ++eye) {
        const View& v = c->views[eye];
        CameraView cv; // per-eye view data (the SSS scale + glass refraction are view-specific)
        cv.viewProj = eyeVP(v);
        cv.view = viewFromPose(v.pos, v.quat);
        cv.pos = v.pos;
        cv.tanL = v.tanL;
        cv.tanR = v.tanR;
        cv.tanU = v.tanU;
        cv.tanD = v.tanD;
        cv.zNear = 0.1f; // eyeVP's hardcoded near/far
        cv.zFar = 300.0f;
        // the view + tonemap UBOs are single-slot host buffers, so each eye is
        // its own fence-waited submit to avoid the next eye clobbering this one
        writeViewUbo(cv, rs.envIntensity, ext);
        renderSceneBuffers(cmd, app, rs.bgColor, cv);
        hdrBarrier(cmd, app);
        if (app.hdrView() != m_tmSrcView)
            rebuildTonemapSet(app);
        const VkRect2D r = eyeRegion(v, ext, eye);
        writeTonemapUbo((float)r.offset.x, (float)r.offset.y, (float)r.extent.width, (float)r.extent.height);
        const bool first = eye == 0;
        const bool last = eye == nViews - 1;
        drawTonemapRegion(cmd, app, fb, r, first);
        if (last && imgui && imgui->CmdLists.Size > 0) {
            vkCmdSetScissor(cmd, 0, 1, &full);
            ImGui_ImplVulkan_RenderDrawData(imgui, cmd);
        }
        vkCmdEndRenderPass(cmd);
        if (xrImg && eye < nXr)
            drawXrEye(cmd, app, eye, xrImg[eye]);
        app.submit(cmd);
        if (!last)
            cmd = app.beginCommands();
    }
}

void Renderer::initXrTarget(VkApp& app, VkFormat fmt, int nEyes, const std::vector<std::vector<VkImage>>& images,
                            const VkExtent2D* exts) {
    m_xrFmt = fmt;
    VkAttachmentDescription att{};
    att.format = fmt;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpc{};
    rpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpc.attachmentCount = 1;
    rpc.pAttachments = &att;
    rpc.subpassCount = 1;
    rpc.pSubpasses = &sub;
    VK(vkCreateRenderPass(m_dev, &rpc, nullptr, &m_xrRp));
    m_xrPipe = makeGraphicsPipeline(m_dev, m_xrRp, tonemap_vert_spv, tonemap_vert_spv_len / 4, tonemap_frag_spv,
                                    tonemap_frag_spv_len / 4, m_tmPl, false);
    vkMakeBuffer(m_dev, m_pdev, m_xrTmUbuf, m_xrTmMem, 32, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    m_xrViews.resize(nEyes);
    m_xrFbs.resize(nEyes);
    for (int i = 0; i < nEyes; ++i) {
        m_xrExt[i] = exts[i];
        m_xrViews[i].resize(images[i].size());
        m_xrFbs[i].resize(images[i].size());
        for (size_t k = 0; k < images[i].size(); ++k) {
            VkImageViewCreateInfo vci{};
            vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vci.image = images[i][k];
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = fmt;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK(vkCreateImageView(m_dev, &vci, nullptr, &m_xrViews[i][k]));
            VkFramebufferCreateInfo fci{};
            fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fci.renderPass = m_xrRp;
            fci.attachmentCount = 1;
            fci.pAttachments = &m_xrViews[i][k];
            fci.width = exts[i].width;
            fci.height = exts[i].height;
            fci.layers = 1;
            VK(vkCreateFramebuffer(m_dev, &fci, nullptr, &m_xrFbs[i][k]));
        }
    }
    m_xrReady = true;
    rebuildTonemapSet(app);
}

void Renderer::drawXrEye(VkCommandBuffer cmd, VkApp& app, int eye, uint32_t imgIdx) {
    if (!m_xrReady)
        return;
    if (app.hdrView() != m_tmSrcView)
        rebuildTonemapSet(app);
    const uint32_t w = m_xrExt[eye].width, h = m_xrExt[eye].height;
    const float tm[8] = {0.0f, 0.0f, (float)w, (float)h, m_exposure, 0.0f, 0.0f, 0.0f};
    vkWriteBuffer(m_dev, m_xrTmMem, tm, 32);
    VkClearValue cv[1]{};
    cv[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    VkRenderPassBeginInfo rpb{};
    rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpb.renderPass = m_xrRp;
    rpb.framebuffer = m_xrFbs[eye][imgIdx];
    rpb.renderArea = {{0, 0}, {w, h}};
    rpb.clearValueCount = 1;
    rpb.pClearValues = cv;
    vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, (float)h, (float)w, -(float)h, 0, 1};
    VkRect2D sc{0, 0, w, h};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_xrPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tmPl, 0, 1, &m_xrSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void Renderer::releaseEnv() {
    if (!m_envReady)
        return;
    for (const auto& p : {m_pcCube, m_pcPref, m_pcIrr}) {
        if (p.pool)
            vkDestroyDescriptorPool(m_dev, p.pool, nullptr);
        if (p.dsl)
            vkDestroyDescriptorSetLayout(m_dev, p.dsl, nullptr);
        if (p.pl)
            vkDestroyPipelineLayout(m_dev, p.pl, nullptr);
        if (p.pipe)
            vkDestroyPipeline(m_dev, p.pipe, nullptr);
    }
    vkFreeImage2D(m_dev, m_envEq.img, m_envEq.mem, m_envEq.view);
    vkFreeImage2D(m_dev, m_envPre.img, m_envPre.mem, m_envPre.view);
    vkFreeImage2D(m_dev, m_envIrr.img, m_envIrr.mem, m_envIrr.view);
    for (VkImageView v : m_envPreMips)
        vkDestroyImageView(m_dev, v, nullptr);
    if (m_envIrrArr)
        vkDestroyImageView(m_dev, m_envIrrArr, nullptr);
    if (m_eqSampler)
        vkDestroySampler(m_dev, m_eqSampler, nullptr);
    if (m_cubeSampler)
        vkDestroySampler(m_dev, m_cubeSampler, nullptr);
    m_envReady = false;
}

void Renderer::shutdown() {
    if (m_dev == VK_NULL_HANDLE)
        return;
    m_entInst.clear();
    m_entTex.clear();
    m_freeInst.clear();
    m_freeTex.clear();
    for (auto& inst : m_insts) // already-flushed slots hold null handles (a no-op free)
        vkFreeBuffer(m_dev, inst.modelUbuf, inst.modelMem);
    for (auto& g : m_geoms) {
        if (!g.externalPosNrm) {
            vkFreeBuffer(m_dev, g.pos, g.posMem);
            vkFreeBuffer(m_dev, g.nrm, g.nrmMem);
        }
        vkFreeBuffer(m_dev, g.uv, g.uvMem);
        vkFreeBuffer(m_dev, g.ibuf, g.ibmem);
    }
    for (auto& t : m_texs)
        vkFreeImage2D(m_dev, t.img, t.mem, t.view);
    releaseSss();
    vkFreeImage2D(m_dev, m_white.img, m_white.mem, m_white.view);
    if (m_texSampler)
        vkDestroySampler(m_dev, m_texSampler, nullptr);
    releaseEnv();
    for (VkDescriptorPool p : m_meshPools)
        vkDestroyDescriptorPool(m_dev, p, nullptr);
    m_meshPools.clear();
    if (m_dsl)
        vkDestroyDescriptorSetLayout(m_dev, m_dsl, nullptr);
    if (m_pl)
        vkDestroyPipelineLayout(m_dev, m_pl, nullptr);
    if (m_pipe)
        vkDestroyPipeline(m_dev, m_pipe, nullptr);
    if (m_pipeBlend)
        vkDestroyPipeline(m_dev, m_pipeBlend, nullptr);
    if (m_pipeBlendCull)
        vkDestroyPipeline(m_dev, m_pipeBlendCull, nullptr);
    for (auto* fb : m_noClearFbs)
        vkDestroyFramebuffer(m_dev, fb, nullptr);
    if (m_noClearRp)
        vkDestroyRenderPass(m_dev, m_noClearRp, nullptr);
    if (m_xrReady) {
        for (auto& eye : m_xrFbs)
            for (auto* fb : eye)
                vkDestroyFramebuffer(m_dev, fb, nullptr);
        for (auto& eye : m_xrViews)
            for (auto* v : eye)
                vkDestroyImageView(m_dev, v, nullptr);
        if (m_xrPipe)
            vkDestroyPipeline(m_dev, m_xrPipe, nullptr);
        if (m_xrRp)
            vkDestroyRenderPass(m_dev, m_xrRp, nullptr);
        vkFreeBuffer(m_dev, m_xrTmUbuf, m_xrTmMem);
    }
    if (m_tmPool)
        vkDestroyDescriptorPool(m_dev, m_tmPool, nullptr);
    if (m_tmDsl)
        vkDestroyDescriptorSetLayout(m_dev, m_tmDsl, nullptr);
    if (m_tmPl)
        vkDestroyPipelineLayout(m_dev, m_tmPl, nullptr);
    if (m_tmPipe)
        vkDestroyPipeline(m_dev, m_tmPipe, nullptr);
    vkFreeBuffer(m_dev, m_tmUbuf, m_tmMem);
    vkFreeBuffer(m_dev, m_vpUbuf, m_vpMem);
    for (uint32_t l = 0; l < kMaxLights * 6; ++l) {
        vkDestroyFramebuffer(m_dev, m_shadowFb[l], nullptr);
        vkDestroyImageView(m_dev, m_shadowColorFace[l], nullptr);
    }
    for (uint32_t f = 0; f < 6; ++f)
        vkDestroyImageView(m_dev, m_shadowDepthFace[f], nullptr);
    vkFreeBuffer(m_dev, m_lightsUbuf, m_lightsMem);
    if (m_shadowPipe)
        vkDestroyPipeline(m_dev, m_shadowPipe, nullptr);
    if (m_shadowPl)
        vkDestroyPipelineLayout(m_dev, m_shadowPl, nullptr);
    if (m_shadowRp)
        vkDestroyRenderPass(m_dev, m_shadowRp, nullptr);
    if (m_shadowSampler)
        vkDestroySampler(m_dev, m_shadowSampler, nullptr);
    if (m_shadowSampleView)
        vkDestroyImageView(m_dev, m_shadowSampleView, nullptr);
    if (m_shadowCube) {
        vkDestroyImage(m_dev, m_shadowCube, nullptr);
        vkFreeMemory(m_dev, m_shadowCubeMem, nullptr);
    }
    if (m_shadowDepth) {
        vkDestroyImage(m_dev, m_shadowDepth, nullptr);
        vkFreeMemory(m_dev, m_shadowDepthMem, nullptr);
    }
}
