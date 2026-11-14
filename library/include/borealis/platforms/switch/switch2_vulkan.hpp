#pragma once
#ifdef __SWITCH2__
#ifndef VK_USE_PLATFORM_VI_NN
#define VK_USE_PLATFORM_VI_NN
#endif
#include <nanovg.h>
#include <vulkan/vulkan.h>

#include <array>
#include <borealis/core/frame_presentation.hpp>
#include <borealis/core/frame_resources.hpp>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

namespace brls
{

struct Switch2VkBuffer
{
    VkBuffer buffer       = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size     = 0;
    VkBufferView luma = VK_NULL_HANDLE, chroma = VK_NULL_HANDLE;
    void* mapped = nullptr;
};

class Switch2VulkanContext
{
  public:
    static Switch2VulkanContext* current();
    Switch2VulkanContext();
    ~Switch2VulkanContext();
    void begin();
    void end();
    void clear(NVGcolor color);
    Switch2VkBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    bool importBuffer(void* address, VkDeviceSize size, Switch2VkBuffer& result, bool texelViews = true);
    void destroyBuffer(Switch2VkBuffer& buffer);
    void setFrameDriven(bool enabled);
    void videoFrameChanged(bool changed) { newVideoFrame |= changed; }
    void videoSource(bool direct, bool texel);
    void wakeFrame();
    void retain(std::shared_ptr<void> resource);
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags required);
    static void check(VkResult rc, const char* operation);
    VkCommandBuffer command() const { return commands[frameIndex]; }

    VkInstance instance       = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device           = VK_NULL_HANDLE;
    VkQueue queue             = VK_NULL_HANDLE;
    uint32_t queueFamily = 0, frameIndex = 0;
    VkRenderPass renderPass   = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, 2> commands { };
    VkExtent2D extent { };
    NVGcontext* vg           = nullptr;
    bool externalHostMemory  = false;
    bool texelBufferSampling = false;
    float videoFPS           = 0;
    std::mutex queueMutex;

  private:
    struct Flight
    {
        VkFence fence        = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        FrameResources references;
    };
    struct Target
    {
        VkImage image = VK_NULL_HANDLE, stencil = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE, stencilView = VK_NULL_HANDLE;
        VkDeviceMemory stencilMemory = VK_NULL_HANDLE;
        VkFramebuffer framebuffer    = VK_NULL_HANDLE;
        VkSemaphore complete         = VK_NULL_HANDLE;
    };
    void recreate();
    void destroyTargets();
    void release(Flight& flight);
    void createVideoViews(Switch2VkBuffer& buffer);
    void consumeAcquiredImage();
    void reportPresentation(uint64_t now);
    VkSurfaceKHR surface     = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM, stencilFormat = VK_FORMAT_D24_UNORM_S8_UINT;
    std::array<Flight, 2> flights { };
    std::vector<Target> targets;
    uint32_t imageIndex = 0;
    bool resizePending  = false;
    bool imageAcquired  = false;
    bool videoDrawn = false, newVideoFrame = false, directVideo = false, texelVideo = false;
    uint32_t maxTexelElements = 0;
    FramePresentation presentation;
    uint64_t statsStart = 0, presents = 0, newFrames = 0, repeats = 0, skipped = 0;
    uint64_t directPresents = 0, copyPresents = 0, texelPresents = 0, storagePresents = 0;
    std::mutex wakeMutex;
    std::condition_variable frameReady;
    uint64_t wakeGeneration = 0, observedGeneration = 0;
};
} // namespace brls
#endif
