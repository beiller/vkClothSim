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
bool loadTexture(const std::string& path, TextureData& out);
bool loadHdr(const std::string& path, HdrData& out);
