#include "gltf.hpp"

#include "assets.hpp"
#include "ecs_ops.hpp"
#include "math.hpp"
#include "tiny_gltf_v3.h"

#include "stb_image.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace {

std::string s(tg3_str v) {
    return v.data ? std::string(v.data, v.len) : std::string();
}

int32_t fsExists(const char* p, uint32_t len, void*) {
    return std::filesystem::exists(std::string(p, len)) ? 1 : 0;
}

int32_t fsRead(uint8_t** out, uint64_t* size, const char* p, uint32_t len, void*) {
    std::ifstream f(std::string(p, len), std::ios::binary);
    if (!f)
        return 0;
    const std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    uint8_t* buf = (uint8_t*)std::malloc(v.size() ? v.size() : 1);
    std::memcpy(buf, v.data(), v.size());
    *out = buf;
    *size = v.size();
    return 1;
}

void fsFree(uint8_t* d, uint64_t, void*) {
    std::free(d);
}

std::vector<uint8_t> b64Decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        return c == '+' ? 62 : (c == '/' ? 63 : -1);
    };
    std::vector<uint8_t> out;
    int acc = 0, n = 0;
    for (char c : in) {
        const int v = val(c);
        if (v < 0)
            continue;
        acc = (acc << 6) | v;
        if (++n == 4) {
            out.insert(out.end(), {(uint8_t)((acc >> 16) & 0xff), (uint8_t)((acc >> 8) & 0xff), (uint8_t)(acc & 0xff)});
            n = 0;
        }
    }
    if (n == 3)
        out.insert(out.end(), {(uint8_t)((acc >> 10) & 0xff), (uint8_t)((acc >> 2) & 0xff)});
    else if (n == 2)
        out.push_back((uint8_t)((acc >> 4) & 0xff));
    return out;
}

// glTF color data is sRGB-encoded, lighting math is linear: decode sRGB -> linear. Data textures (normal, metallicRoughness, occlusion) are already linear, must not be decoded
static float srgbToLinear(float s) {
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}
static void decodeSrgb(std::vector<uint8_t>& rgba) {
    for (size_t i = 0; i < rgba.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            rgba[i + c] = (uint8_t)(srgbToLinear(rgba[i + c] / 255.0f) * 255.0f + 0.5f);
}

bool decodeImage(const tg3_model& m, int32_t imgIdx, const std::string& baseDir, std::vector<uint8_t>& out, int& outW,
                 int& outH, bool srgb) {
    if (imgIdx < 0 || (uint32_t)imgIdx >= m.images_count)
        return false;
    const tg3_image& img = m.images[imgIdx];
    std::vector<uint8_t> file;
    bool embedded = false;
    if (img.buffer_view >= 0 && (uint32_t)img.buffer_view < m.buffer_views_count) {
        const tg3_buffer_view& bv = m.buffer_views[img.buffer_view];
        if ((uint32_t)bv.buffer >= m.buffers_count)
            return false;
        const tg3_buffer& buf = m.buffers[bv.buffer];
        if (bv.byte_offset + bv.byte_length > buf.data.count)
            return false;
        file.assign(buf.data.data + bv.byte_offset, buf.data.data + bv.byte_offset + bv.byte_length);
        embedded = true;
    } else if (img.uri.data && strncmp(img.uri.data, "data:", 5) == 0) {
        const std::string uri = s(img.uri);
        const size_t comma = uri.find(',');
        if (comma == std::string::npos || uri.find("base64") == std::string::npos)
            return false;
        file = b64Decode(uri.substr(comma + 1));
        embedded = true;
    } else if (img.uri.data) {
        TextureData td;
        if (!loadTexture(baseDir + "/" + s(img.uri), td, false))
            return false;
        out = std::move(td.rgba);
        outW = td.w;
        outH = td.h;
    } else
        return false;
    if (embedded) {
        stbi_set_flip_vertically_on_load(0);
        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &ch, 4);
        if (!px)
            return false;
        out.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
        outW = w;
        outH = h;
    }
    if (srgb)
        decodeSrgb(out);
    return true;
}

int compSize(int ct) {
    switch (ct) {
    case TG3_COMPONENT_TYPE_BYTE:
    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
        return 1;
    case TG3_COMPONENT_TYPE_SHORT:
    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
        return 2;
    case TG3_COMPONENT_TYPE_INT:
    case TG3_COMPONENT_TYPE_UNSIGNED_INT:
    case TG3_COMPONENT_TYPE_FLOAT:
        return 4;
    case TG3_COMPONENT_TYPE_DOUBLE:
        return 8;
    }
    return 0;
}

int typeComps(int t) {
    switch (t) {
    case TG3_TYPE_SCALAR:
        return 1;
    case TG3_TYPE_VEC2:
        return 2;
    case TG3_TYPE_VEC3:
        return 3;
    case TG3_TYPE_VEC4:
        return 4;
    case TG3_TYPE_MAT2:
        return 4;
    case TG3_TYPE_MAT3:
        return 9;
    case TG3_TYPE_MAT4:
        return 16;
    }
    return 0;
}

float rawComp(const uint8_t* p, int ct) {
    switch (ct) {
    case TG3_COMPONENT_TYPE_BYTE:
        return (float)(int8_t)p[0];
    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
        return (float)p[0];
    case TG3_COMPONENT_TYPE_SHORT:
        return (float)int16_t(p[0] | p[1] << 8);
    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
        return (float)uint16_t(p[0] | p[1] << 8);
    case TG3_COMPONENT_TYPE_INT: {
        int32_t v;
        std::memcpy(&v, p, 4);
        return (float)v;
    }
    case TG3_COMPONENT_TYPE_UNSIGNED_INT: {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return (float)v;
    }
    case TG3_COMPONENT_TYPE_FLOAT: {
        float f;
        std::memcpy(&f, p, 4);
        return f;
    }
    case TG3_COMPONENT_TYPE_DOUBLE: {
        double d;
        std::memcpy(&d, p, 8);
        return (float)d;
    }
    }
    return 0.0f;
}

float normInt(const uint8_t* p, int ct) {
    switch (ct) {
    case TG3_COMPONENT_TYPE_BYTE:
        return ((int8_t)p[0] + 128) / 128.0f;
    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
        return p[0] / 255.0f;
    case TG3_COMPONENT_TYPE_SHORT:
        return (int16_t(p[0] | p[1] << 8) + 32768) / 32768.0f;
    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
        return uint16_t(p[0] | p[1] << 8) / 65535.0f;
    }
    return 0.0f;
}

bool readAccessor(const tg3_model& m, int32_t accIdx, int wantComps, std::vector<float>& out) {
    if (accIdx < 0 || (uint32_t)accIdx >= m.accessors_count)
        return false;
    const tg3_accessor& a = m.accessors[accIdx];
    const int comps = typeComps(a.type);
    if (comps != wantComps || a.sparse.is_sparse)
        return false;
    if (a.buffer_view < 0) {
        out.assign((size_t)a.count * comps, 0.0f);
        return true;
    }
    if ((uint32_t)a.buffer_view >= m.buffer_views_count || (uint32_t)m.buffer_views[a.buffer_view].buffer >= m.buffers_count)
        return false;
    const tg3_buffer_view& bv = m.buffer_views[a.buffer_view];
    const tg3_buffer& buf = m.buffers[bv.buffer];
    const int cs = compSize(a.component_type);
    if (cs == 0)
        return false;
    const uint64_t elem = (uint64_t)cs * comps;
    const uint64_t stride = bv.byte_stride ? bv.byte_stride : elem;
    if (bv.byte_offset + a.byte_offset + stride * a.count > buf.data.count)
        return false;
    const uint8_t* base = buf.data.data + bv.byte_offset + a.byte_offset;
    out.resize((size_t)a.count * comps);
    if (a.normalized && cs < 4) {
        for (uint64_t i = 0; i < a.count; ++i)
            for (int c = 0; c < comps; ++c)
                out[i * comps + c] = normInt(base + stride * i + c * cs, a.component_type);
        return true;
    }
    for (uint64_t i = 0; i < a.count; ++i)
        for (int c = 0; c < comps; ++c)
            out[i * comps + c] = rawComp(base + stride * i + c * cs, a.component_type);
    return true;
}

int32_t attrAccessor(const tg3_primitive& p, const char* name) {
    for (uint32_t i = 0; i < p.attributes_count; ++i)
        if (s(p.attributes[i].key) == name)
            return p.attributes[i].value;
    return -1;
}

void computeNormals(const std::vector<float>& pos, const std::vector<uint32_t>& idx, std::vector<float>& nrm) {
    nrm.assign(pos.size(), 0.0f);
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const V3 a{pos[idx[i] * 3], pos[idx[i] * 3 + 1], pos[idx[i] * 3 + 2]};
        const V3 b{pos[idx[i + 1] * 3], pos[idx[i + 1] * 3 + 1], pos[idx[i + 1] * 3 + 2]};
        const V3 c{pos[idx[i + 2] * 3], pos[idx[i + 2] * 3 + 1], pos[idx[i + 2] * 3 + 2]};
        const V3 n = vCross(vSub(b, a), vSub(c, a));
        for (int j = 0; j < 3; ++j) {
            const size_t v = idx[i + j] * 3;
            nrm[v] += n.x;
            nrm[v + 1] += n.y;
            nrm[v + 2] += n.z;
        }
    }
    for (size_t i = 0; i < nrm.size(); i += 3) {
        V3 n{
            nrm[i], nrm[i + 1], nrm[i + 2]};
        if (vLen(n) < 1e-12f)
            n = {0, 0, 1};
        n = vNorm(n);
        nrm[i] = n.x;
        nrm[i + 1] = n.y;
        nrm[i + 2] = n.z;
    }
}

bool packPrimitive(const tg3_model& m, const tg3_mesh& gm, const tg3_primitive& p, Mesh& out, std::string& warn) {
    if (p.mode != -1 && p.mode != 4) {
        warn = "primitive mode is not triangles";
        return false;
    }
    std::vector<float> pos, nrm, uv, idxf;
    if (!readAccessor(m, attrAccessor(p, "POSITION"), 3, pos)) {
        warn = "missing or invalid POSITION";
        return false;
    }
    readAccessor(m, attrAccessor(p, "NORMAL"), 3, nrm);
    if (!readAccessor(m, attrAccessor(p, "TEXCOORD_0"), 2, uv))
        uv.assign(pos.size() / 3 * 2, 0.0f);
    const uint64_t n = pos.size() / 3;
    if (p.indices >= 0) {
        if (!readAccessor(m, p.indices, 1, idxf)) {
            warn = "missing or invalid indices";
            return false;
        }
        for (float v : idxf)
            if (v < 0.0f || v >= (float)n) {
                warn = "index out of range";
                return false;
            }
    } else
        for (uint64_t i = 0; i < n; ++i)
            idxf.push_back((float)i);
    bool baked = false, skipped = false;
    for (uint32_t t = 0; t < p.targets_count; ++t) {
        const float w = t < gm.weights_count ? (float)gm.weights[t] : 0.0f;
        if (std::fabs(w) < 1e-6f)
            continue;
        int32_t acc = -1;
        for (uint32_t i = 0; i < p.target_attribute_counts[t]; ++i)
            if (s(p.targets[t][i].key) == "POSITION")
                acc = p.targets[t][i].value;
        std::vector<float> d;
        if (acc < 0 || !readAccessor(m, acc, 3, d) || d.size() != pos.size()) {
            skipped = true;
            continue;
        }
        for (size_t i = 0; i < pos.size(); ++i)
            pos[i] += w * d[i];
        baked = true;
    }
    if (skipped)
        warn = "some morph targets have no POSITION delta (only POSITION is applied); skipped";
    if (nrm.empty() || baked) {
        std::vector<uint32_t> idx(idxf.begin(), idxf.end());
        computeNormals(pos, idx, nrm);
    }
    out.pos = std::move(pos);
    out.nrm = std::move(nrm);
    out.uv = std::move(uv);
    for (float v : idxf)
        out.indices.push_back((uint32_t)v);
    return true;
}

V4 mat3ToQuat(const V3& r0, const V3& r1, const V3& r2) {
    const float m00 = r0.x, m01 = r0.y, m02 = r0.z;
    const float m10 = r1.x, m11 = r1.y, m12 = r1.z;
    const float m20 = r2.x, m21 = r2.y, m22 = r2.z;
    const float tr = m00 + m11 + m22;
    V4 q;
    if (tr > 0.0f) {
        const float s = 0.5f / std::sqrt(tr + 1.0f);
        q = {(m21 - m12) * s, (m02 - m20) * s, (m10 - m01) * s, 0.25f / s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}

bool nodePose(const tg3_node& n, Transform& out) {
    if (!n.has_matrix) {
        out = {{(float)n.translation[0], (float)n.translation[1], (float)n.translation[2]},
               {(float)n.rotation[0], (float)n.rotation[1], (float)n.rotation[2], (float)n.rotation[3]},
               {(float)n.scale[0], (float)n.scale[1], (float)n.scale[2]}};
        if (out.scale.x < 1e-9f || out.scale.y < 1e-9f || out.scale.z < 1e-9f)
            return false;
        return true;
    }
    const double* M = n.matrix;
    out.pos = {(float)M[12], (float)M[13], (float)M[14]};
    V3 c0{(float)M[0], (float)M[1], (float)M[2]};
    V3 c1{(float)M[4], (float)M[5], (float)M[6]};
    V3 c2{(float)M[8], (float)M[9], (float)M[10]};
    out.scale = {vLen(c0), vLen(c1), vLen(c2)};
    if (out.scale.x < 1e-9f || out.scale.y < 1e-9f || out.scale.z < 1e-9f)
        return false;
    c0 = vScale(c0, 1.0f / out.scale.x);
    c1 = vScale(c1, 1.0f / out.scale.y);
    c2 = vScale(c2, 1.0f / out.scale.z);
    if (vDot(vCross(c0, c1), c2) < 0.0f) {
        c2 = vScale(c2, -1.0f);
        out.scale.z = -out.scale.z;
    }
    c2 = vNorm(vSub(c2, vScale(c1, vDot(c2, c1))));
    c1 = vNorm(vSub(c1, vScale(c0, vDot(c1, c0))));
    out.quat = mat3ToQuat({c0.x, c1.x, c2.x}, {c0.y, c1.y, c2.y}, {c0.z, c1.z, c2.z});
    return true;
}

struct GltfCtx {
    entt::registry& reg;
    const tg3_model* m;
    std::string fileBase;
    std::string fileId;
    std::string baseDir;
    entt::entity root;
    std::vector<entt::entity> materials;
    std::map<std::pair<int32_t, bool>, entt::entity> textures;
    struct PendingPrim {
        int32_t nodeIdx, meshIdx, primIdx;
    };
    std::vector<entt::entity> nodeEntities;
    std::vector<entt::entity> skinChars;
    std::vector<PendingPrim> pending;
    std::vector<std::string> usedNames;
};

void warn(GltfCtx& c, const std::string& msg) {
    std::fprintf(stderr, "3dsim: gltf %s: %s\n", c.fileBase.c_str(), msg.c_str());
}

std::string imageId(const GltfCtx& c, int32_t imgIdx) {
    const tg3_image& img = c.m->images[imgIdx];
    if (img.uri.data && img.buffer_view < 0 && strncmp(img.uri.data, "data:", 5) != 0)
        return c.baseDir + "/" + s(img.uri);
    return c.fileId + "#img" + std::to_string(imgIdx);
}

entt::entity textureForImage(GltfCtx& c, int32_t imgIdx, bool srgb) {
    if (imgIdx < 0 || (uint32_t)imgIdx >= c.m->images_count)
        return entt::null;
    const auto key = std::make_pair(imgIdx, srgb);
    const auto it = c.textures.find(key);
    if (it != c.textures.end() && it->second != entt::null)
        return it->second;
    std::vector<uint8_t> px;
    int wpx = 0, hpx = 0;
    if (!decodeImage(*c.m, imgIdx, c.baseDir, px, wpx, hpx, srgb)) {
        warn(c, "cannot decode image " + std::to_string(imgIdx));
        return entt::null;
    }
    return c.textures[key] = makeTexture(c.reg, imageId(c, imgIdx), px.data(), wpx, hpx);
}

entt::entity textureForTexture(GltfCtx& c, int32_t texIdx, bool srgb) {
    if (texIdx < 0 || (uint32_t)texIdx >= c.m->textures_count)
        return entt::null;
    return textureForImage(c, c.m->textures[texIdx].source, srgb);
}

entt::entity materialFor(GltfCtx& c, int32_t matIdx) {
    if (matIdx >= 0 && (uint32_t)matIdx < c.materials.size() && c.materials[matIdx] != entt::null)
        return c.materials[matIdx];
    if (matIdx < 0 || (uint32_t)matIdx >= c.m->materials_count)
        return spawnMaterial(c.reg, V3{1, 1, 1}, 0.0f, 1.0f, V3{0, 0, 0}, nullptr);
    const tg3_material& gm = c.m->materials[matIdx];
    const auto& pbr = gm.pbr_metallic_roughness;
    const std::string name = gm.name.data ? s(gm.name) : "mat_" + std::to_string(matIdx);
    std::string full = c.fileBase + "/" + name;
    for (int n = 1; std::find(c.usedNames.begin(), c.usedNames.end(), full) != c.usedNames.end(); ++n)
        full = c.fileBase + "/" + name + "." + std::to_string(n);
    c.usedNames.push_back(full);
    const V3 baseColor = {srgbToLinear((float)pbr.base_color_factor[0]), srgbToLinear((float)pbr.base_color_factor[1]),
                         srgbToLinear((float)pbr.base_color_factor[2])};
    const V3 emissive = {srgbToLinear((float)gm.emissive_factor[0]), srgbToLinear((float)gm.emissive_factor[1]),
                        srgbToLinear((float)gm.emissive_factor[2])};
    const entt::entity e =
        spawnMaterial(c.reg, baseColor, (float)pbr.metallic_factor, (float)pbr.roughness_factor, emissive, full.c_str());
    auto& mat = c.reg.get<Material>(e);
    const auto slot = [&](int32_t texIdx, int32_t coord, const char* what, entt::entity* field, bool srgb) {
        if (texIdx < 0)
            return;
        if (coord != 0) {
            warn(c, std::string(what) + " uses uv channel " + std::to_string(coord) + " (only uv0 is packed); map skipped");
            return;
        }
        *field = textureForTexture(c, texIdx, srgb);
    };
    slot(pbr.base_color_texture.index, pbr.base_color_texture.tex_coord, "baseColor", &mat.tex, true);
    slot(pbr.metallic_roughness_texture.index, pbr.metallic_roughness_texture.tex_coord, "metallicRoughness", &mat.texMR, false);
    slot(gm.normal_texture.index, gm.normal_texture.tex_coord, "normal", &mat.texNormal, false);
    if (gm.normal_texture.index >= 0)
        mat.normalScale = (float)gm.normal_texture.scale;
    slot(gm.emissive_texture.index, gm.emissive_texture.tex_coord, "emissive", &mat.texEmissive, true);
    mat.alpha = (float)pbr.base_color_factor[3];
    mat.alphaCutoff = (float)gm.alpha_cutoff;
    const std::string am = s(gm.alpha_mode);
    mat.alphaMode = am == "BLEND" ? 2 : (am == "MASK" ? 1 : 0);
    for (uint32_t i = 0; i < gm.ext.extensions_count; ++i) {
        if (s(gm.ext.extensions[i].name) != "KHR_materials_transmission")
            continue;
        const tg3_value& ext = gm.ext.extensions[i].value;
        double tf = 1.0;
        for (uint32_t j = 0; j < ext.object_count; ++j) {
            const auto& od = ext.object_data[j];
            if (s(od.key) == "transmissionFactor" && od.value.type == TG3_VALUE_REAL)
                tf = od.value.real_val;
            if (s(od.key) == "transmissionTexture" && od.value.type == TG3_VALUE_INT && od.value.int_val >= 0)
                warn(c, "transmissionTexture is not supported (ignored; use transmissionFactor)");
        }
        if (tf > 0.0)
            mat.refraction = (float)tf; // 1:1: transmissionFactor drives the refraction amount (0..1)
        break;
    }
    for (uint32_t i = 0; i < gm.ext.extensions_count; ++i) {
        if (s(gm.ext.extensions[i].name) != "KHR_materials_ior")
            continue;
        const tg3_value& ext = gm.ext.extensions[i].value;
        double io = 1.5;
        for (uint32_t j = 0; j < ext.object_count; ++j) {
            const auto& od = ext.object_data[j];
            if (s(od.key) == "ior" && od.value.type == TG3_VALUE_REAL)
                io = od.value.real_val;
            if (s(od.key) == "iorTexture" && od.value.type == TG3_VALUE_INT && od.value.int_val >= 0)
                warn(c, "iorTexture is not supported (ignored; use ior)");
        }
        mat.ior = (float)io;
        break;
    }
    // KHR_materials_volume is the glTF SSS encoding: baseColorTransmission is the
    // transmitted fraction; attenuation/colour are ignored (the SSS pass is a
    // scalar-strength blur, Godot style)
    for (uint32_t i = 0; i < gm.ext.extensions_count; ++i) {
        if (s(gm.ext.extensions[i].name) != "KHR_materials_volume")
            continue;
        const tg3_value& ext = gm.ext.extensions[i].value;
        float bt[3] = {0.0f, 0.0f, 0.0f};
        bool haveBt = false, warnedAttenuation = false;
        for (uint32_t j = 0; j < ext.object_count; ++j) {
            const auto& od = ext.object_data[j];
            const std::string k = s(od.key);
            if (k == "baseColorTransmission" && od.value.type == TG3_VALUE_ARRAY && od.value.array_count == 3) {
                for (uint32_t n = 0; n < 3; ++n)
                    if (od.value.array_data[n].type == TG3_VALUE_REAL)
                        bt[n] = (float)od.value.array_data[n].real_val;
                haveBt = true;
            }
            if ((k == "baseColorTransmissionTexture" || k == "thicknessTexture") && od.value.type == TG3_VALUE_INT &&
                od.value.int_val >= 0)
                warn(c, k + " is not supported (ignored)");
            if ((k == "attenuationColor" || k == "attenuationColorExtinction" || k == "attenuationDistance" || k == "thickness") &&
                !warnedAttenuation) {
                warn(c, "KHR_materials_volume attenuation/thickness are not supported (ignored; volume maps to ssssWeight only)");
                warnedAttenuation = true;
            }
        }
        if (haveBt)
            mat.ssssWeight = V3{bt[0], bt[1], bt[2]};
        break;
    }
    return c.materials[matIdx] = e;
}

int32_t skinOf(const GltfCtx& c, int32_t nodeIdx) {
    const int32_t s = c.m->nodes[nodeIdx].skin;
    if (s >= 0)
        return s;
    return c.m->skins_count == 1 ? 0 : -1;
}

int32_t jointAcc(const tg3_primitive& p) {
    const int32_t a = attrAccessor(p, "JOINTS_0");
    return a >= 0 ? a : attrAccessor(p, "JOINT_0");
}

std::vector<std::string> targetNames(const tg3_mesh& gm) {
    std::vector<std::string> out;
    if (!gm.ext.extras || gm.ext.extras->type != TG3_VALUE_OBJECT)
        return out;
    const tg3_value& ex = *gm.ext.extras;
    for (uint32_t i = 0; i < ex.object_count; ++i) {
        if (s(ex.object_data[i].key) != "targetNames")
            continue;
        const tg3_value& arr = ex.object_data[i].value;
        if (arr.type != TG3_VALUE_ARRAY)
            break;
        for (uint32_t j = 0; j < arr.array_count; ++j)
            if (arr.array_data[j].type == TG3_VALUE_STRING)
                out.push_back(s(arr.array_data[j].string_val));
        break;
    }
    return out;
}

// each skinned prim keeps its full rest data (pos/nrm/joints/weights + shapes) in its own Mesh;
// Skin::sync lazily adopts a per-mesh GPU state from it
void finishSkinned(GltfCtx& c) {
    for (const auto& pp : c.pending) {
        const int32_t skin = skinOf(c, pp.nodeIdx);
        const tg3_mesh& gm = c.m->meshes[pp.meshIdx];
        const tg3_primitive& p = gm.primitives[pp.primIdx];
        if (p.mode != -1 && p.mode != 4) {
            warn(c, "skinned prim: primitive mode is not triangles");
            continue;
        }
        if (c.skinChars[skin] == entt::null) {
            const tg3_skin& sk = c.m->skins[skin];
            const std::string skelName = s(sk.name);
            std::vector<entt::entity> jointEntities;
            jointEntities.resize(sk.joints_count);
            for (uint32_t i = 0; i < sk.joints_count; ++i) {
                const int32_t ji = sk.joints[i];
                jointEntities[i] = (uint32_t)ji < c.m->nodes_count ? c.nodeEntities[ji] : entt::null;
            }
            std::vector<float> ibf;
            const bool haveIbm =
                sk.inverse_bind_matrices >= 0 && readAccessor(*c.m, sk.inverse_bind_matrices, 16, ibf) &&
                ibf.size() == 16 * sk.joints_count;
            if (!haveIbm)
                warn(c, "skin " + std::to_string(skin) + ": missing or bad inverseBindMatrices (identity)");
            std::vector<Mat4> invBinds;
            for (uint32_t i = 0; i < sk.joints_count; ++i) {
                Mat4 m = mat4Identity();
                if (haveIbm)
                    std::memcpy(m.m, ibf.data() + 16 * i, 64);
                invBinds.push_back(m);
            }
            for (uint32_t i = 0; i < sk.joints_count; ++i) {
                const int32_t ji = sk.joints[i];
                if ((uint32_t)ji >= c.m->nodes_count)
                    continue;
                const tg3_node& jn = c.m->nodes[ji];
                if (!jn.has_matrix && (std::fabs(jn.scale[0] - jn.scale[1]) > 1e-4 || std::fabs(jn.scale[1] - jn.scale[2]) > 1e-4)) {
                    warn(c, "joint " + s(jn.name) + " has non-uniform scale (approximated by the DQ scale blend)");
                    break;
                }
            }
            Skeleton skelComp;
            skelComp.name = skelName;
            for (uint32_t i = 0; i < sk.joints_count; ++i)
                skelComp.boneNames.push_back((uint32_t)sk.joints[i] < c.m->nodes_count ? s(c.m->nodes[sk.joints[i]].name) : "");
            skelComp.joints = std::move(jointEntities);
            skelComp.invBinds = std::move(invBinds);
            skelComp.placement = c.nodeEntities[pp.nodeIdx];
            const entt::entity skelEnt = c.reg.create();
            c.reg.emplace<Name>(skelEnt, skelName);
            c.reg.emplace<Skeleton>(skelEnt, std::move(skelComp));
            c.reg.emplace<Parent>(skelEnt, c.root);
            c.skinChars[skin] = skelEnt;
        }
        const entt::entity skelEnt = c.skinChars[skin];
        if (skelEnt == entt::null)
            continue;
        const int nJ = (int)c.m->skins[skin].joints_count;

        std::vector<float> pos0;
        if (!readAccessor(*c.m, attrAccessor(p, "POSITION"), 3, pos0) || pos0.empty()) {
            warn(c, "skinned prim: bad POSITION");
            continue;
        }
        const int nV = (int)(pos0.size() / 3);
        Mesh pm;
        pm.pos = std::move(pos0);
        std::vector<float> nrm0;
        if (readAccessor(*c.m, attrAccessor(p, "NORMAL"), 3, nrm0) && nrm0.size() == (size_t)nV * 3)
            pm.nrm = std::move(nrm0);
        else {
            std::vector<uint32_t> idx;
            std::vector<float> idxf;
            if (p.indices >= 0 && readAccessor(*c.m, p.indices, 1, idxf))
                for (float v : idxf)
                    idx.push_back((uint32_t)v);
            computeNormals(pm.pos, idx, pm.nrm);
        }
        const int32_t ja = jointAcc(p);
        const int32_t wa = attrAccessor(p, "WEIGHTS_0") >= 0 ? attrAccessor(p, "WEIGHTS_0") : attrAccessor(p, "WEIGHT_0");
        std::vector<float> jf, wf;
        if (!readAccessor(*c.m, ja, 4, jf) || jf.size() != (size_t)4 * nV) {
            warn(c, "skinned prim: bad JOINTS_0");
            continue;
        }
        const int wc = wa >= 0 ? typeComps(c.m->accessors[wa].type) : 0;
        const bool haveW = wc > 0 && readAccessor(*c.m, wa, wc, wf) && wf.size() == (size_t)wc * nV;
        if (wc > 0 && !haveW)
            warn(c, "skinned prim: bad WEIGHTS_0 (treating as zero)");
        pm.joints.resize((size_t)4 * nV);
        pm.weights.assign((size_t)4 * nV, 0.0f);
        bool badJoint = false;
        for (int v = 0; v < nV; ++v) {
            float sum = 0.0f;
            for (int i = 0; i < 4; ++i) {
                const float jv = jf[4 * v + i];
                const bool okJ = jv >= 0.0f && jv < (float)nJ;
                if (!okJ)
                    badJoint = true;
                pm.joints[4 * v + i] = okJ ? (uint32_t)jv : 0;
                const float wgt = (i < wc && haveW) ? wf[wc * v + i] : 0.0f;
                pm.weights[4 * v + i] = wgt;
                sum += wgt;
            }
            if (sum > 1e-6f)
                for (int i = 0; i < 4; ++i)
                    pm.weights[4 * v + i] /= sum;
        }
        if (badJoint)
            warn(c, "skinned prim: joint index out of range (clamped)");
        std::vector<float> uv, idxf;
        if (!readAccessor(*c.m, attrAccessor(p, "TEXCOORD_0"), 2, uv) || uv.size() != (size_t)nV * 2)
            uv.assign((size_t)nV * 2, 0.0f);
        pm.uv = std::move(uv);
        bool badIdx = false;
        if (p.indices >= 0) {
            if (!readAccessor(*c.m, p.indices, 1, idxf))
                badIdx = true;
            for (float v : idxf) {
                if (v < 0.0f || v >= (float)nV) {
                    badIdx = true;
                    break;
                }
                pm.indices.push_back((uint32_t)v);
            }
        } else
            for (int i = 0; i < nV; ++i)
                pm.indices.push_back((uint32_t)i);
        if (badIdx) {
            warn(c, "skinned prim: index out of range");
            continue;
        }
        const std::string mName = s(gm.name);
        const std::string cname = mName.empty() ? "prim" : (gm.primitives_count > 1 ? mName + "_" + std::to_string(pp.primIdx) : mName);
        const std::vector<std::string> names = targetNames(gm);
        for (int t = 0; t < (int)p.targets_count; ++t) {
            SubMesh sh;
            sh.name = t < (int)names.size() && !names[t].empty() ? names[t] : "target" + std::to_string(t);
            if (t < (int)gm.weights_count)
                sh.weight = (float)gm.weights[t];
            int32_t acc = -1;
            bool hasNrm = false;
            for (uint32_t i = 0; i < p.target_attribute_counts[t]; ++i) {
                const std::string k = s(p.targets[t][i].key);
                if (k == "POSITION")
                    acc = p.targets[t][i].value;
                else if (k == "NORMAL")
                    hasNrm = true;
            }
            if (acc >= 0) {
                std::vector<float> d;
                if (readAccessor(*c.m, acc, 3, d) && d.size() == (size_t)nV * 3)
                    sh.pos = std::move(d);
                else
                    warn(c, "skinned prim " + cname + ": target " + std::to_string(t) + " POSITION delta mismatch (slot zeroed)");
            } else if (hasNrm)
                warn(c, "skinned prim " + cname + ": target " + std::to_string(t) + " has no POSITION delta (slot zeroed)");
            pm.shapes.push_back(std::move(sh));
        }
        const entt::entity child = c.reg.create();
        c.reg.emplace<Name>(child, cname);
        c.reg.emplace<Geometry>(child, Geometry{std::move(pm)});
        addMesh(c.reg, child, child, materialFor(c, p.material), c.nodeEntities[pp.nodeIdx]);
        c.reg.get<Skeleton>(skelEnt).meshes.push_back(child);
    }
}

entt::entity buildNode(GltfCtx& c, int32_t nodeIdx, entt::entity parent) {
    if (nodeIdx < 0 || (uint32_t)nodeIdx >= c.m->nodes_count)
        return entt::null;
    const tg3_node& n = c.m->nodes[nodeIdx];
    Transform t{};
    if (!nodePose(n, t)) {
        warn(c, "node " + s(n.name) + ": degenerate transform");
        return entt::null;
    }
    entt::entity e = c.reg.create();
    const std::string name = s(n.name).empty() ? "node" + std::to_string(nodeIdx) : s(n.name);
    c.reg.emplace<Name>(e, c.fileBase + "/" + name);
    c.reg.emplace<Transform>(e, t);
    c.nodeEntities[nodeIdx] = e;
    if (parent != entt::null)
        c.reg.emplace<Parent>(e, parent);
    if (n.mesh >= 0 && (uint32_t)n.mesh < c.m->meshes_count) {
        const tg3_mesh& gm = c.m->meshes[n.mesh];
        const std::string mName = s(gm.name);
        const int32_t skin = skinOf(c, nodeIdx);
        for (uint32_t i = 0; i < gm.primitives_count; ++i) {
            const tg3_primitive& p = gm.primitives[i];
            if (skin >= 0 && jointAcc(p) >= 0) {
                c.pending.push_back({nodeIdx, n.mesh, (int32_t)i});
                continue;
            }
            if (jointAcc(p) >= 0)
                warn(c, "skinned mesh " + mName + ": no usable skin, using bind pose");
            Mesh pm;
            std::string pwarn;
            const bool ok = packPrimitive(*c.m, gm, p, pm, pwarn);
            if (!pwarn.empty())
                warn(c, "mesh " + mName + " primitive " + std::to_string(i) + ": " + pwarn);
            if (!ok || pm.vertexCount() == 0)
                continue;
            const entt::entity child = c.reg.create();
            const std::string cname = mName.empty() ? "prim" : (gm.primitives_count > 1 ? mName + "_" + std::to_string(i) : mName);
            c.reg.emplace<Name>(child, cname);
            entt::entity geomEnt = c.reg.create();
            c.reg.emplace<Geometry>(geomEnt, Geometry{pm});
            addMesh(c.reg, child, geomEnt, materialFor(c, p.material), e);
        }
    }
    for (uint32_t i = 0; i < n.children_count; ++i)
        buildNode(c, n.children[i], e);
    return e;
}

}

entt::entity loadGltf(entt::registry& reg, entt::entity e, const std::string& path, std::string& err) {
    std::string file;
    if (std::filesystem::exists("assets/" + path))
        file = "assets/" + path;
    else if (std::filesystem::exists(exeDir() + "/../assets/" + path))
        file = exeDir() + "/../assets/" + path;
    else {
        err = "gltf file not found: " + path;
        return entt::null;
    }
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        err = "gltf file unreadable: " + path;
        return entt::null;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::string baseDir = std::filesystem::path(file).parent_path().string();
    const std::string fileBase = std::filesystem::path(file).stem().string();

    tg3_parse_options opt;
    tg3_parse_options_init(&opt);
    opt.fs.file_exists = fsExists;
    opt.fs.read_file = fsRead;
    opt.fs.free_file = fsFree;

    tg3_model model;
    tg3_error_stack es;
    tg3_error_stack_init(&es);
    const tg3_error_code rc = tg3_parse_auto(&model, &es, bytes.data(), bytes.size(), baseDir.c_str(),
                                             (uint32_t)baseDir.size(), &opt);
    if (rc != TG3_OK) {
        err = es.count && es.entries ? es.entries[0].message : "gltf parse failed";
        return entt::null;
    }
    for (uint32_t i = 0; i < model.extensions_required_count; ++i)
        if (s(model.extensions_required[i]) == "KHR_draco_mesh_compression") {
            err = "DRACO-compressed gltf is not supported";
            tg3_model_free(&model);
            return entt::null;
        }
    if (!model.scenes_count) {
        err = "gltf has no scene";
        tg3_model_free(&model);
        return entt::null;
    }
    const int32_t sceneIdx =
        model.default_scene >= 0 && (uint32_t)model.default_scene < model.scenes_count ? model.default_scene : 0;
    const tg3_scene& scene = model.scenes[sceneIdx];
    if (!scene.nodes_count) {
        err = "gltf scene has no nodes";
        tg3_model_free(&model);
        return entt::null;
    }
    if (!reg.try_get<Transform>(e))
        reg.emplace<Transform>(e);
    GltfCtx c{reg, &model, fileBase, file, baseDir, e, {}, {}};
    // note: value-initialized entt::entity is 0, not entt::null (all bits set in EnTT 4)
    c.materials.assign(model.materials_count, entt::null);
    c.nodeEntities.assign(model.nodes_count, entt::null);
    c.skinChars.assign(model.skins_count, entt::null);
    for (uint32_t i = 0; i < scene.nodes_count; ++i)
        buildNode(c, scene.nodes[i], e);
    finishSkinned(c);
    tg3_model_free(&model);
    return e;
}
