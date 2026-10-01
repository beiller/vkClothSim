#pragma once
#include <vulkan/vulkan.h>

#include "xr_frame.hpp"
#include <vector>

class VkApp;

// VR device backend; the core only sees XrFrame + Vk types
struct IXrBackend {
    virtual ~IXrBackend() = default;
    // create the Vulkan device via the runtime (must run before the device is allocated);
    // false = use VkApp::makeDevice()
    virtual bool createDevice(VkApp& app) = 0;
    virtual VkDevice device() const = 0;
    virtual bool init(VkApp& app) = 0;
    virtual void shutdown() = 0;
    // per-frame: events + wait/begin frame + head pose
    virtual XrFrame poll() = 0;
    // must run once per poll() while the session is running
    virtual void endFrame(bool render) = 0;
    // swapchain targets for direct headset rendering
    virtual int eyeCount() const = 0;
    virtual VkFormat format() const = 0;
    virtual VkExtent2D extent(int eye) const = 0;
    virtual const std::vector<VkImage>& images(int eye) const = 0;
    virtual void acquireImage(int eye) = 0;
    virtual void releaseImage(int eye) = 0;
    virtual int imageIndex(int eye) const = 0;
};
