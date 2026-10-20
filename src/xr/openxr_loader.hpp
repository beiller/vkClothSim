#pragma once
#include "openxr.h"

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
    X(xrGetActionStateFloat)                                                                                           \
    X(xrGetActionStateVector2f)                                                                                        \
    X(xrCreateActionSpace)                                                                                             \
    X(xrStringToPath)                                                                                                  \
    X(xrPathToString)                                                                                                  \
    X(xrGetCurrentInteractionProfile)                                                                                  \
    X(xrEnumerateInstanceExtensionProperties)

struct XrLoader {
    bool load();
    bool ready() const { return m_handle != nullptr; }

#define XR_PFN(name) PFN_##name name = nullptr;
    XR_FUNCS(XR_PFN)
#undef XR_PFN

private:
    void* m_handle = nullptr;
};
