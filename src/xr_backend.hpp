#pragma once
#include <vulkan/vulkan.h>

#include "xr_frame.hpp"
#include <vector>

class VkApp;

struct IXrBackend {
    virtual ~IXrBackend() = default;
    virtual bool createDevice(VkApp& app) = 0;
    virtual VkDevice device() const = 0;
    virtual bool init(VkApp& app) = 0;
    virtual void shutdown() = 0;
    virtual void waitFrame() {}
    virtual XrFrame poll() = 0;
    virtual void endFrame(bool render) = 0;
    virtual int eyeCount() const = 0;
    virtual VkFormat format() const = 0;
    virtual VkExtent2D extent(int eye) const = 0;
    virtual const std::vector<VkImage>& images(int eye) const = 0;
    virtual void acquireImage(int eye) = 0;
    virtual void releaseImage(int eye) = 0;
    virtual int imageIndex(int eye) const = 0;
};
