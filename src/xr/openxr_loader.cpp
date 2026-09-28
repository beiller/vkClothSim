#include "xr/openxr_loader.hpp"

#include <dlfcn.h>

bool XrLoader::load(const char* libName) {
    m_handle = dlopen(libName, RTLD_NOW | RTLD_GLOBAL);
    if (!m_handle)
        return false;
    bool ok = true;
#define XR_LOAD(name)                                                                                                  \
    name = (PFN_##name)dlsym(m_handle, #name);                                                                         \
    if (!name)                                                                                                         \
        ok = false;
    XR_FUNCS(XR_LOAD)
#undef XR_LOAD
    if (!ok) {
        dlclose(m_handle);
        m_handle = nullptr;
    }
    return ok;
}
