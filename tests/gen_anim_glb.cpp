// One-off generator for assets/anim_test.glb: root -> jointA (rotation 0->90deg Z over 1s),
// root -> jointB (translation (0,0,0)->(1,0,0) over 1s). Run: g++ gen_anim_glb.cpp -o gen && ./gen
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main() {
    const float s45 = std::sqrt(0.5f);
    std::vector<float> bin;
    auto push = [&](const float* v, int n) {
        for (int i = 0; i < n; ++i)
            bin.push_back(v[i]);
    };
    const float times[2] = {0.0f, 1.0f};
    push(times, 2); // bv0: rotation times
    const float rot[8] = {0, 0, 0, 1, 0, 0, s45, s45};
    push(rot, 8); // bv1: rotation values (identity -> 90deg Z)
    push(times, 2); // bv2: translation times
    const float trans[6] = {0, 0, 0, 1, 0, 0};
    push(trans, 6); // bv3: translation values
    const std::string json = R"({
 "asset": {"version": "2.0"},
 "buffers": [{"byteLength": )" + std::to_string(bin.size() * 4) + R"(}],
 "bufferViews": [
   {"buffer": 0, "byteOffset": 0, "byteLength": 8},
   {"buffer": 0, "byteOffset": 8, "byteLength": 32},
   {"buffer": 0, "byteOffset": 40, "byteLength": 8},
   {"buffer": 0, "byteOffset": 48, "byteLength": 24}
 ],
 "accessors": [
   {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR"},
   {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC4"},
   {"bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR"},
   {"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3"}
 ],
 "nodes": [
   {"name": "root", "children": [1, 2]},
   {"name": "jointA", "translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
   {"name": "jointB", "translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}
 ],
 "scenes": [{"name": "Scene", "nodes": [0]}],
 "scene": 0,
 "animations": [{
   "name": "spin",
   "channels": [
     {"sampler": 0, "target": {"node": 1, "path": "rotation"}},
     {"sampler": 1, "target": {"node": 2, "path": "translation"}}
   ],
   "samplers": [
     {"input": 0, "output": 1, "interpolation": "LINEAR"},
     {"input": 2, "output": 3}
   ]
 }]
})";
    std::vector<uint8_t> out;
    auto u32 = [&](uint32_t v) {
        out.push_back(v & 0xff);
        out.push_back((v >> 8) & 0xff);
        out.push_back((v >> 16) & 0xff);
        out.push_back((v >> 24) & 0xff);
    };
    std::string js = json;
    while (js.size() % 4)
        js += ' ';
    const uint32_t binLen = (uint32_t)(bin.size() * 4);
    const uint32_t total = 12 + 8 + js.size() + 8 + binLen;
    u32(0x46546C67); // "glTF"
    u32(2);
    u32(total);
    u32((uint32_t)js.size());
    u32(0x4E4F534A); // "JSON"
    for (char c : js)
        out.push_back((uint8_t)c);
    u32(binLen);
    u32(0x004E4942); // "BIN\0"
    for (float f : bin) {
        uint32_t b;
        std::memcpy(&b, &f, 4);
        out.push_back(b & 0xff);
        out.push_back((b >> 8) & 0xff);
        out.push_back((b >> 16) & 0xff);
        out.push_back((b >> 24) & 0xff);
    }
    std::ofstream f("assets/anim_test.glb", std::ios::binary);
    f.write((const char*)out.data(), (std::streamsize)out.size());
    std::printf("wrote assets/anim_test.glb (%zu bytes)\n", out.size());
    return 0;
}
