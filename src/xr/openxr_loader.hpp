#pragma once
#include "openxr.h"

// dlopen/dlsym wrapper over the OpenXR runtime (libopenxr.so.1). The runtime is
// loaded lazily so the windowed build works with no OpenXR present; when absent
// load() fails and --xr reports it instead of crashing.

// Every entry point we resolve, in one list, shared by the declarations below and
// the dlsym loop in openxr_loader.cpp.
#define XR_FUNCS(X)                                                                                                    \
    X(xrCreateInstance)                                                                                                \
    X(xrDestroyInstance)                                                                                               \
    X(xrGetSystem)                                                                                                     \
    X(xrGetSystemProperties)                                                                                           \
    X(xrCreateSession)                                                                                                 \
    X(xrDestroySession)                                                                                                \
    X(xrBeginSession)                                                                                                  \
    X(xrEndSession)                                                                                                    \
    X(xrCreateReferenceSpace)                                                                                          \
    X(xrWaitFrame)                                                                                                     \
    X(xrLocateViews)                                                                                                   \
    X(xrCreateSwapchain)                                                                                               \
    X(xrDestroySwapchain)                                                                                              \
    X(xrAcquireSwapchainImage)                                                                                         \
    X(xrReleaseSwapchainImage)                                                                                         \
    X(xrEnumerateSwapchainFormats)                                                                                     \
    X(xrEnumerateViewConfigurationViews)                                                                               \
    X(xrEnumerateSwapchainImages)                                                                                      \
    X(xrLocateSpace)                                                                                                   \
    X(xrDestroySpace)                                                                                                  \
    X(xrEndFrame)                                                                                                      \
    X(xrCreateActionSet)                                                                                               \
    X(xrDestroyActionSet)                                                                                              \
    X(xrCreateAction)                                                                                                  \
    X(xrAttachSessionActionSets)                                                                                       \
    X(xrSuggestInteractionProfileBindings)                                                                             \
    X(xrSyncActions)                                                                                                   \
    X(xrEnumerateBoundSourcesForAction)                                                                                \
    X(xrGetActionStateBoolean)

struct XrLoader {
    bool load(const char* libName = "libopenxr.so.1");
    bool ready() const { return m_handle != nullptr; }
    void* handle() const { return m_handle; }

#define XR_PFN(name) PFN_##name name = nullptr
    XR_FUNCS(XR_PFN)
#undef XR_PFN

private:
    void* m_handle = nullptr;
};
