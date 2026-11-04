#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct TextureData {
    std::vector<uint8_t> rgba;
    int w = 0;
    int h = 0;
};

struct HdrData {
    std::vector<float> rgb;
    int w = 0;
    int h = 0;
};

std::string exeDir();
std::string assetsRoot();
bool loadTexture(const std::string& path, TextureData& out, bool flip = true);
bool loadHdr(const std::string& path, HdrData& out);

// decoded pixels of glb-embedded images, keyed by the "file#imgN" id (the texture
// upload path falls back to this when the path is not a file)
void registerEmbeddedTexture(const std::string& id, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);
bool loadEmbeddedTexture(const std::string& id, TextureData& out);
