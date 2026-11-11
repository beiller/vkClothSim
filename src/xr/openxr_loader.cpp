#include "xr/openxr_loader.hpp"

#include "assets.hpp"
#include <dlfcn.h>
#include <string>
#include <vector>

namespace {
std::vector<std::string> vendoredPaths() {
    std::vector<std::string> p;
    const std::string exe = exeDir();
    if (!exe.empty())
        p.emplace_back(exe + "/../lib/openxr/lib/libopenxr.so.1");
    p.emplace_back("lib/openxr/lib/libopenxr.so.1");
    return p;
}
}

bool XrLoader::load() {
    for (const std::string& p : vendoredPaths()) {
        m_handle = dlopen(p.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (m_handle)
            break;
        m_error = dlerror();
    }
    if (!m_handle)
        return false;
    bool ok = true;
// NOLINTNEXTLINE(bugprone-macro-parentheses): name is pasted into PFN_##name, cannot be parenthesized
#define XR_LOAD(name)                                                                                                  \
    do {                                                                                                               \
        name = (PFN_##name)dlsym(m_handle, #name); /* NOLINT(bugprone-macro-parentheses) */                           \
        if (!name) /* NOLINT(bugprone-macro-parentheses) */                                                            \
            ok = false;                                                                                                \
    } while (0);
    XR_FUNCS(XR_LOAD)
#undef XR_LOAD
    if (!ok) {
        dlclose(m_handle);
        m_handle = nullptr;
    }
    return ok;
}
