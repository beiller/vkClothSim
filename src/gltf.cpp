#include "gltf.hpp"

#include "assets.hpp"
#include "math.hpp"
#include "systems.hpp"
#include "tiny_gltf_v3.h"

#include "stb_image.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

std::string s(tg3_str v) {
    return v.data ? std::string(v.data, v.len) : std::string();
}

// ---- filesystem callbacks for external refs (.gltf + separate .bin / .png); the parser
// already resolves base_dir + uri, so the path arrives ready to use

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

// base64 decode (data:image/...;base64,... image URIs in .gltf files)
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

// resolve + decode a glTF image to RGBA8 (bufferView bytes, data URI, or external file)
bool decodeImage(const tg3_model& m, int32_t imgIdx, const std::string& baseDir, std::vector<uint8_t>& out, int& outW,
                 int& outH) {
    if (imgIdx < 0 || (uint32_t)imgIdx >= m.images_count)
        return false;
    const tg3_image& img = m.images[imgIdx];
    std::vector<uint8_t> file;
    if (img.buffer_view >= 0 && (uint32_t)img.buffer_view < m.buffer_views_count) {
        const tg3_buffer_view& bv = m.buffer_views[img.buffer_view];
        if ((uint32_t)bv.buffer >= m.buffers_count)
            return false;
        const tg3_buffer& buf = m.buffers[bv.buffer];
        if (bv.byte_offset + bv.byte_length > buf.data.count)
            return false;
        file.assign(buf.data.data + bv.byte_offset, buf.data.data + bv.byte_offset + bv.byte_length);
    } else if (img.uri.data && strncmp(img.uri.data, "data:", 5) == 0) {
        const std::string uri = s(img.uri);
        const size_t comma = uri.find(',');
        if (comma == std::string::npos || uri.find("base64") == std::string::npos)
            return false;
        file = b64Decode(uri.substr(comma + 1));
    } else if (img.uri.data) {
        // external file: unflipped (glTF uv convention: v0 = top of image)
        TextureData td;
        if (!loadTexture(baseDir + "/" + s(img.uri), td, false))
            return false;
        out = std::move(td.rgba);
        outW = td.w;
        outH = td.h;
        return true;
    } else
        return false;
    // glTF uv convention: (0,0) = top-left, matching Vulkan's default origin, so no row flip
    stbi_set_flip_vertically_on_load(0);
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &ch, 4);
    if (!px)
        return false;
    out.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    outW = w;
    outH = h;
    return true;
}

// ---- accessors

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
    }
    return 0;
}

// one component (raw value, not normalized) as float
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

// read accessor elements as floats (out sized count*comps); handles byteStride, normalized ints,
// float64; false if the accessor is missing/foreign to the wanted layout
bool readAccessor(const tg3_model& m, int32_t accIdx, int wantComps, std::vector<float>& out) {
    if (accIdx < 0 || (uint32_t)accIdx >= m.accessors_count)
        return false;
    const tg3_accessor& a = m.accessors[accIdx];
    const int comps = typeComps(a.type);
    if (comps != wantComps || a.sparse.is_sparse)
        return false;
    if (a.buffer_view < 0) {
        out.assign((size_t)a.count * comps, 0.0f); // sparse-only accessor: zeros
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

// accessor index of a primitive attribute ("POSITION", "NORMAL", "TEXCOORD_0", ...); -1 if absent
int32_t attrAccessor(const tg3_primitive& p, const char* name) {
    for (uint32_t i = 0; i < p.attributes_count; ++i)
        if (s(p.attributes[i].key) == name)
            return p.attributes[i].value;
    return -1;
}

// ---- geometry

// area-weighted vertex normals from triangle indices
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

// pack one glTF primitive into the engine's Mesh (pos/nrm/uv0 + u32 indices); NORMAL computed
// if absent, UVs zero if absent
bool packPrimitive(const tg3_model& m, const tg3_primitive& p, Mesh& out, std::string& warn) {
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
    if (nrm.empty()) {
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

// ---- node transforms

// rows of R (local->world rotation, the quatRotate convention) -> unit quaternion
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

// a node's TRS (or decomposed matrix) into a pose; near-zero scale is rejected
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
    if (vDot(vCross(c0, c1), c2) < 0.0f) { // reflection (e.g. a single negative scale)
        c2 = vScale(c2, -1.0f);
        out.scale.z = -out.scale.z;
    }
    // kill shear (Gram-Schmidt, last axis first)
    c2 = vNorm(vSub(c2, vScale(c1, vDot(c2, c1))));
    c1 = vNorm(vSub(c1, vScale(c0, vDot(c1, c0))));
    out.quat = mat3ToQuat({c0.x, c1.x, c2.x}, {c0.y, c1.y, c2.y}, {c0.z, c1.z, c2.z});
    return true;
}

// ---- load context: per-file caches of the created shared entities

struct GltfCtx {
    World& w;
    const tg3_model* m;
    std::string fileBase; // file name without extension (name prefix)
    std::string fileId;   // resolved path (embedded-image dedupe key)
    std::string baseDir;
    std::vector<entt::entity> materials; // per glTF material index (null = not created)
    std::vector<entt::entity> textures;  // per glTF image index
};

void warn(GltfCtx& c, const std::string& msg) {
    std::fprintf(stderr, "3dsim: gltf %s: %s\n", c.fileBase.c_str(), msg.c_str());
}

// identity of a glTF image: resolved path for external files, "file#imgN" for embedded
std::string imageId(const GltfCtx& c, int32_t imgIdx) {
    const tg3_image& img = c.m->images[imgIdx];
    if (img.uri.data && img.buffer_view < 0 && strncmp(img.uri.data, "data:", 5) != 0)
        return c.baseDir + "/" + s(img.uri);
    return c.fileId + "#img" + std::to_string(imgIdx);
}

// shared texture entity for a glTF image (deduped globally by identity)
entt::entity textureForImage(GltfCtx& c, int32_t imgIdx) {
    if (imgIdx < 0 || (uint32_t)imgIdx >= c.m->images_count)
        return entt::null;
    if (c.textures[imgIdx] != entt::null)
        return c.textures[imgIdx];
    std::vector<uint8_t> px;
    int wpx = 0, hpx = 0;
    if (!decodeImage(*c.m, imgIdx, c.baseDir, px, wpx, hpx)) {
        warn(c, "cannot decode image " + std::to_string(imgIdx));
        return entt::null;
    }
    return c.textures[imgIdx] = textureBytes(c.w, imageId(c, imgIdx), px.data(), wpx, hpx);
}

// a glTF texture -> its image (several textures may share one image)
entt::entity textureForTexture(GltfCtx& c, int32_t texIdx) {
    if (texIdx < 0 || (uint32_t)texIdx >= c.m->textures_count)
        return entt::null;
    return textureForImage(c, c.m->textures[texIdx].source);
}

// shared material entity for a glTF material (created once per index within this load); a
// missing index yields the glTF default material (white, fully rough)
entt::entity materialFor(GltfCtx& c, int32_t matIdx) {
    if (matIdx >= 0 && (uint32_t)matIdx < c.materials.size() && c.materials[matIdx] != entt::null)
        return c.materials[matIdx];
    if (matIdx < 0 || (uint32_t)matIdx >= c.m->materials_count)
        return spawnMaterial(c.w, V3{1, 1, 1}, 0.0f, 1.0f, V3{0, 0, 0}, nullptr);
    const tg3_material& gm = c.m->materials[matIdx];
    const auto& pbr = gm.pbr_metallic_roughness;
    const std::string name = gm.name.data ? s(gm.name) : "mat_" + std::to_string(matIdx);
    const entt::entity e =
        spawnMaterial(c.w, {(float)pbr.base_color_factor[0], (float)pbr.base_color_factor[1], (float)pbr.base_color_factor[2]},
                      (float)pbr.metallic_factor, (float)pbr.roughness_factor,
                      {(float)gm.emissive_factor[0], (float)gm.emissive_factor[1], (float)gm.emissive_factor[2]},
                      (c.fileBase + "/" + name).c_str());
    auto& mat = c.w.reg.get<Material>(e);
    const auto slot = [&](int32_t texIdx, int32_t coord, const char* what, entt::entity* field) {
        if (texIdx < 0)
            return;
        if (coord != 0) {
            warn(c, std::string(what) + " uses uv channel " + std::to_string(coord) + " (only uv0 is packed); map skipped");
            return;
        }
        *field = textureForTexture(c, texIdx);
    };
    slot(pbr.base_color_texture.index, pbr.base_color_texture.tex_coord, "baseColor", &mat.tex);
    slot(pbr.metallic_roughness_texture.index, pbr.metallic_roughness_texture.tex_coord, "metallicRoughness", &mat.texMR);
    slot(gm.normal_texture.index, gm.normal_texture.tex_coord, "normal", &mat.texNormal);
    slot(gm.emissive_texture.index, gm.emissive_texture.tex_coord, "emissive", &mat.texEmissive);
    return c.materials[matIdx] = e;
}

// a node -> entity with local Transform + Parent; mesh primitives -> child mesh entities.
// the node scale lives in the Transform (composed by the world-transform resolve)
entt::entity buildNode(GltfCtx& c, int32_t nodeIdx, entt::entity parent) {
    if (nodeIdx < 0 || (uint32_t)nodeIdx >= c.m->nodes_count)
        return entt::null;
    const tg3_node& n = c.m->nodes[nodeIdx];
    Transform t{};
    if (!nodePose(n, t)) {
        warn(c, "node " + s(n.name) + ": degenerate transform");
        return entt::null;
    }
    entt::entity e = c.w.reg.create();
    const std::string name = s(n.name).empty() ? "node" + std::to_string(nodeIdx) : s(n.name);
    c.w.reg.emplace<Name>(e, c.fileBase + "/" + name);
    c.w.reg.emplace<Transform>(e, t);
    if (parent != entt::null)
        c.w.reg.emplace<Parent>(e, parent);
    if (n.mesh >= 0 && (uint32_t)n.mesh < c.m->meshes_count) {
        const tg3_mesh& gm = c.m->meshes[n.mesh];
        const std::string mName = s(gm.name);
        if (n.skin >= 0)
            warn(c, "skinned mesh " + mName + ": no animation support, using bind pose");
        bool targets = false;
        for (uint32_t i = 0; i < gm.primitives_count; ++i) {
            const tg3_primitive& p = gm.primitives[i];
            targets = targets || p.targets_count > 0;
            Mesh pm;
            std::string pwarn;
            if (!packPrimitive(*c.m, p, pm, pwarn)) {
                warn(c, "mesh " + mName + " primitive " + std::to_string(i) + ": " + pwarn);
                continue;
            }
            if (pm.vertexCount() == 0)
                continue;
            const entt::entity child = c.w.reg.create();
            const std::string cname = mName.empty() ? "prim" : (gm.primitives_count > 1 ? mName + "_" + std::to_string(i) : mName);
            c.w.reg.emplace<Name>(child, cname);
            addMesh(c.w, child, c.w.renderer.addMesh(pm).geom, materialFor(c, p.material), e);
        }
        if (targets)
            warn(c, "mesh " + mName + ": morph targets (shape keys) skipped");
    }
    for (uint32_t i = 0; i < n.children_count; ++i)
        buildNode(c, n.children[i], e);
    return e;
}

} // namespace

entt::entity loadGltf(World& w, entt::entity e, const std::string& path, std::string& err) {
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
    if (!w.reg.try_get<Transform>(e))
        w.reg.emplace<Transform>(e);
    GltfCtx c{w, &model, fileBase, file, baseDir, {}, {}};
    // note: value-initialized entt::entity is 0, not entt::null (all bits set in EnTT 4)
    c.materials.assign(model.materials_count, entt::null);
    c.textures.assign(model.images_count, entt::null);
    for (uint32_t i = 0; i < scene.nodes_count; ++i)
        buildNode(c, scene.nodes[i], e);
    tg3_model_free(&model);
    return e;
}
