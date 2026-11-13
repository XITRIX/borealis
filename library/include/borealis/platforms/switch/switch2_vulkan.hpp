#pragma once
#ifdef __SWITCH2__
#ifndef VK_USE_PLATFORM_VI_NN
#define VK_USE_PLATFORM_VI_NN
#endif
#include <nanovg.h>
#include <vulkan/vulkan.h>

#include <array>
#include <borealis/core/frame_resources.hpp>
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
    void* mapped          = nullptr;
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
    bool importBuffer(void* address, VkDeviceSize size, Switch2VkBuffer& result);
    void destroyBuffer(Switch2VkBuffer& buffer);
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
    NVGcontext* vg          = nullptr;
    bool externalHostMemory = false;
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
    VkSurfaceKHR surface     = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM, stencilFormat = VK_FORMAT_D24_UNORM_S8_UINT;
    std::array<Flight, 2> flights { };
    std::vector<Target> targets;
    uint32_t imageIndex = 0;
    bool resizePending  = false;
};
} // namespace brls
#endif
