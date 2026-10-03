#define STB_IMAGE_IMPLEMENTATION
#include "assets.hpp"

#include "stb_image.h"
#include "tinyexr.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

std::string exeDir() {
    char buf[4096];
    size_t n = 0;
#if defined(__APPLE__)
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0)
        return "";
    n = strlen(buf);
#else
    n = (size_t)readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n == 0)
        return "";
    buf[n] = '\0';
#endif
    std::string p(buf, n);
    const size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? "" : p.substr(0, slash);
}

bool loadTexture(const std::string& path, TextureData& out) {
    stbi_set_flip_vertically_on_load(1);
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!px)
        return false;
    out.w = w;
    out.h = h;
    out.rgba.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    return true;
}

bool loadHdr(const std::string& path, HdrData& out) {
    float* rgba = nullptr;
    int w = 0, h = 0;
    const char* err = nullptr;
    const int ret = LoadEXR(&rgba, &w, &h, path.c_str(), &err);
    if (ret < 0) {
        if (err) {
            std::fprintf(stderr, "loadHdr %s: %s\n", path.c_str(), err);
            FreeEXRErrorMessage(err);
        }
        return false;
    }
    out.w = w;
    out.h = h;
    out.rgb.assign((size_t)w * h * 3, 0.0f);
    for (int i = 0; i < w * h; ++i) {
        out.rgb[(size_t)i * 3 + 0] = rgba[(size_t)i * 4 + 0];
        out.rgb[(size_t)i * 3 + 1] = rgba[(size_t)i * 4 + 1];
        out.rgb[(size_t)i * 3 + 2] = rgba[(size_t)i * 4 + 2];
    }
    std::free(rgba);
    return true;
}
