#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct TextureData {
    std::vector<uint8_t> rgba;
    int w = 0;
    int h = 0;
};

std::string exeDir();
bool loadTexture(const std::string& path, TextureData& out);
