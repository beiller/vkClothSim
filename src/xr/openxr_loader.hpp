#pragma once
#include "openxr.h"

// dlopen/dlsym wrapper over the vendored OpenXR loader (lib/openxr/lib/libopenxr.so.1,
// resolved relative to the executable). Loaded lazily so plain window mode works with no
// HMD present; when the loader/runtime is absent load() fails and startup falls back to
// window mode.

// Every entry point we resolve, in one list, shared by the declarations below and
// the dlsym loop in openxr_loader.cpp.
#define XR_FUNCS(X)                                                                                                    \
    X(xrCreateInstance)                                                                                                \
    X(xrDestroyInstance)                                                                                               \
    X(xrGetSystem)                                                                                                     \
    X(xrGetInstanceProcAddr)                                                                                           \
    X(xrGetSystemProperties)                                                                                           \
    X(xrEnumerateEnvironmentBlendModes)                                                                                \
    X(xrPollEvent)                                                                                                     \
    X(xrCreateSession)                                                                                                 \
    X(xrRequestExitSession)                                                                                            \
    X(xrDestroySession)                                                                                                \
    X(xrBeginSession)                                                                                                  \
    X(xrEndSession)                                                                                                    \
    X(xrCreateReferenceSpace)                                                                                          \
    X(xrWaitFrame)                                                                                                     \
    X(xrBeginFrame)                                                                                                    \
    X(xrLocateViews)                                                                                                   \
    X(xrCreateSwapchain)                                                                                               \
    X(xrDestroySwapchain)                                                                                              \
    X(xrAcquireSwapchainImage)                                                                                         \
    X(xrWaitSwapchainImage)                                                                                            \
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
    X(xrDestroyAction)                                                                                                 \
    X(xrAttachSessionActionSets)                                                                                       \
    X(xrSuggestInteractionProfileBindings)                                                                             \
    X(xrSyncActions)                                                                                                   \
    X(xrEnumerateBoundSourcesForAction)                                                                                \
    X(xrGetActionStateBoolean)                                                                                         \
    X(xrGetActionStateVector2f)                                                                                        \
    X(xrCreateActionSpace)                                                                                             \
    X(xrStringToPath)                                                                                                  \
    X(xrPathToString)                                                                                                  \
    X(xrGetCurrentInteractionProfile)                                                                                  \
    X(xrEnumerateInstanceExtensionProperties)

struct XrLoader {
    bool load();
    bool ready() const { return m_handle != nullptr; }
    void* handle() const { return m_handle; }

#define XR_PFN(name) PFN_##name name = nullptr;
    XR_FUNCS(XR_PFN)
#undef XR_PFN

private:
    void* m_handle = nullptr;
};
