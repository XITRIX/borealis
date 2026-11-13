#ifdef __SWITCH2__
#include <nanovg_vk.h>
#include <switch.h>

#include <algorithm>
#include <borealis.hpp>
#include <borealis/platforms/sdl/sdl_video.hpp>
#include <borealis/platforms/switch/switch2_vulkan.hpp>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace brls
{
namespace
{
    Switch2VulkanContext* active = nullptr;
    std::unique_ptr<Switch2VulkanContext> context;
    bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name)
    {
        return std::any_of(list.begin(), list.end(), [name](const auto& entry)
            { return !std::strcmp(entry.extensionName, name); });
    }
    VkImageView imageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect)
    {
        VkImageViewCreateInfo info { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        info.image            = image;
        info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        info.format           = format;
        info.subresourceRange = { aspect, 0, 1, 0, 1 };
        VkImageView view;
        Switch2VulkanContext::check(vkCreateImageView(device, &info, nullptr, &view), "image view");
        return view;
    }
}

Switch2VulkanContext* Switch2VulkanContext::current() { return active; }

void Switch2VulkanContext::check(VkResult rc, const char* operation)
{
    if (rc != VK_SUCCESS)
    {
        brls::Logger::error("SWITCH2_VULKAN: {} failed VkResult={}", operation, int(rc));
        throw std::runtime_error(std::string("Vulkan ") + operation + " failed: " + std::to_string(int(rc)));
    }
}

Switch2VulkanContext::Switch2VulkanContext()
{
    nwindowSetModeDimensions(nwindowGetDefault(), 0, 0, 0, 0);
    nwindowSetFrameRates(nwindowGetDefault(), 120, 60, true);
    const char* instanceExtensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_NN_VI_SURFACE_EXTENSION_NAME };
    VkApplicationInfo app { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "Borealis";
    app.apiVersion       = VK_API_VERSION_1_1;
    VkInstanceCreateInfo create { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    create.pApplicationInfo        = &app;
    create.enabledExtensionCount   = 2;
    create.ppEnabledExtensionNames = instanceExtensions;
    check(vkCreateInstance(&create, nullptr, &instance), "create instance");
    VkViSurfaceCreateInfoNN vi { VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN };
    vi.window = nwindowGetDefault();
    check(vkCreateViSurfaceNN(instance, &vi, nullptr, &surface), "VI surface");
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "physical device count");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "physical devices");
    for (auto candidate : devices)
    {
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> properties(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, properties.data());
        for (uint32_t i = 0; i < families; ++i)
        {
            VkBool32 present = false;
            check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "present support");
            if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
            {
                physical    = candidate;
                queueFamily = i;
                break;
            }
        }
        if (physical)
            break;
    }
    if (!physical)
        throw std::runtime_error("No Vulkan graphics/presentation queue");
    check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr), "extension count");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()), "extensions");
    std::vector<const char*> enabled { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    externalHostMemory = hasExtension(extensions, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    if (externalHostMemory)
        enabled.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    const float priority = 1;
    VkDeviceQueueCreateInfo queues { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queues.queueFamilyIndex = queueFamily;
    queues.queueCount       = 1;
    queues.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceInfo.queueCreateInfoCount    = 1;
    deviceInfo.pQueueCreateInfos       = &queues;
    deviceInfo.enabledExtensionCount   = enabled.size();
    deviceInfo.ppEnabledExtensionNames = enabled.data();
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create device");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    VkPhysicalDeviceProperties properties { };
    vkGetPhysicalDeviceProperties(physical, &properties);
    brls::Logger::info("SWITCH2_VULKAN: native driver={} externalHostMemory={} (UI and video; no GL/Zink)", properties.deviceName, externalHostMemory);
    VkCommandPoolCreateInfo pool { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pool.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queueFamily;
    check(vkCreateCommandPool(device, &pool, nullptr, &commandPool), "command pool");
    VkCommandBufferAllocateInfo allocation { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocation.commandPool        = commandPool;
    allocation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = commands.size();
    check(vkAllocateCommandBuffers(device, &allocation, commands.data()), "command buffers");
    for (auto& flight : flights)
    {
        VkFenceCreateInfo fence { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fence, nullptr, &flight.fence), "frame fence");
        VkSemaphoreCreateInfo semaphore { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        check(vkCreateSemaphore(device, &semaphore, nullptr, &flight.acquired), "acquire semaphore");
    }
    recreate();
    VKNVGCreateInfo nvg { };
    nvg.gpu                      = physical;
    nvg.device                   = device;
    nvg.renderpass               = renderPass;
    nvg.cmdBuffer                = commands.data();
    nvg.frameCount               = commands.size();
    nvg.swapchainImageCount      = targets.size();
    nvg.currentFrame             = &frameIndex;
    nvg.graphicsQueueFamilyIndex = queueFamily; // Backend owns a separate upload command pool.
    vg                           = nvgCreateVk(&nvg, NVG_ANTIALIAS | NVG_STENCIL_STROKES, queue);
    if (!vg)
        throw std::runtime_error("NanoVG Vulkan context creation failed");
    active = this;
}

uint32_t Switch2VulkanContext::memoryType(uint32_t bits, VkMemoryPropertyFlags required)
{
    VkPhysicalDeviceMemoryProperties props { };
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & required) == required)
            return i;
    throw std::runtime_error("No compatible Vulkan memory type");
}

Switch2VkBuffer Switch2VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
    Switch2VkBuffer result { };
    result.size = size;
    VkBufferCreateInfo info { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size  = size;
    info.usage = usage;
    check(vkCreateBuffer(device, &info, nullptr, &result.buffer), "create buffer");
    VkMemoryRequirements requirements { };
    vkGetBufferMemoryRequirements(device, result.buffer, &requirements);
    VkMemoryAllocateInfo memory { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memory.allocationSize  = requirements.size;
    memory.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device, &memory, nullptr, &result.memory), "allocate buffer");
    check(vkBindBufferMemory(device, result.buffer, result.memory, 0), "bind buffer");
    check(vkMapMemory(device, result.memory, 0, VK_WHOLE_SIZE, 0, &result.mapped), "map buffer");
    return result;
}

void Switch2VulkanContext::destroyBuffer(Switch2VkBuffer& buffer)
{
    if (buffer.mapped)
        vkUnmapMemory(device, buffer.memory);
    if (buffer.buffer)
        vkDestroyBuffer(device, buffer.buffer, nullptr);
    if (buffer.memory)
        vkFreeMemory(device, buffer.memory, nullptr);
    buffer = { };
}

bool Switch2VulkanContext::importBuffer(void* address, VkDeviceSize size, Switch2VkBuffer& result)
{
    if (!externalHostMemory)
        return false;
    auto getHost = reinterpret_cast<PFN_vkGetMemoryHostPointerPropertiesEXT>(vkGetDeviceProcAddr(device, "vkGetMemoryHostPointerPropertiesEXT"));
    if (!getHost)
        return false;
    VkMemoryHostPointerPropertiesEXT host { VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT };
    VkResult rc = getHost(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, address, &host);
    if (rc != VK_SUCCESS)
    {
        brls::Logger::warning("SWITCH2_VK_IMPORT: host pointer rejected rc={}", int(rc));
        return false;
    }
    VkExternalMemoryBufferCreateInfo external { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO };
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    VkBufferCreateInfo info { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.pNext = &external;
    info.size  = size;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    rc         = vkCreateBuffer(device, &info, nullptr, &result.buffer);
    if (rc != VK_SUCCESS)
        return false;
    VkMemoryRequirements req { };
    vkGetBufferMemoryRequirements(device, result.buffer, &req);
    const uint32_t bits = host.memoryTypeBits & req.memoryTypeBits;
    if (!bits || req.size > size)
    {
        destroyBuffer(result);
        return false;
    }
    VkImportMemoryHostPointerInfoEXT imported { VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT };
    imported.handleType   = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    imported.pHostPointer = address;
    VkMemoryAllocateInfo memory { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memory.pNext           = &imported;
    memory.allocationSize  = size;
    memory.memoryTypeIndex = memoryType(bits, 0);
    rc                     = vkAllocateMemory(device, &memory, nullptr, &result.memory);
    if (rc == VK_SUCCESS)
        rc = vkBindBufferMemory(device, result.buffer, result.memory, 0);
    brls::Logger::info("SWITCH2_VK_IMPORT: preimport bytes={} VkResult={}", size, int(rc));
    if (rc != VK_SUCCESS)
    {
        destroyBuffer(result);
        return false;
    }
    result.size = size;
    return true;
}

void Switch2VulkanContext::release(Flight& flight)
{
    flight.references.release();
}
void Switch2VulkanContext::retain(std::shared_ptr<void> resource)
{
    flights[frameIndex].references.retain(std::move(resource));
}

void Switch2VulkanContext::destroyTargets()
{
    for (auto& target : targets)
    {
        vkDestroyFramebuffer(device, target.framebuffer, nullptr);
        vkDestroyImageView(device, target.view, nullptr);
        vkDestroyImageView(device, target.stencilView, nullptr);
        vkDestroyImage(device, target.stencil, nullptr);
        vkFreeMemory(device, target.stencilMemory, nullptr);
        vkDestroySemaphore(device, target.complete, nullptr);
    }
    targets.clear();
    if (swapchain)
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

void Switch2VulkanContext::recreate()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    check(vkDeviceWaitIdle(device), "resize idle");
    for (auto& flight : flights)
        release(flight);
    destroyTargets();
    VkSurfaceCapabilitiesKHR caps { };
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "surface capabilities");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        throw std::runtime_error("VI swapchain lacks color-attachment support");
    uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr), "surface formats count");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()), "surface formats");
    auto selected = std::find_if(formats.begin(), formats.end(), [](const auto& f)
        { return f.format == VK_FORMAT_R8G8B8A8_UNORM; });
    if (selected == formats.end())
        selected = std::find_if(formats.begin(), formats.end(), [](const auto& f)
            { return f.format == VK_FORMAT_B8G8R8A8_UNORM; });
    if (selected == formats.end())
        throw std::runtime_error("No unorm swapchain format");
    if (renderPass && selected->format != format)
        throw std::runtime_error("Swapchain format changed incompatibly");
    format = selected->format;
    extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        u32 w = 1920, h = 1080;
        nwindowGetDimensions(nwindowGetDefault(), &w, &h);
        extent = { std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width), std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height) };
    }
    if (!renderPass)
    {
        VkFormatProperties depth { };
        vkGetPhysicalDeviceFormatProperties(physical, stencilFormat, &depth);
        if (!(depth.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
            stencilFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
        VkAttachmentDescription attachments[2] { };
        attachments[0].format         = format;
        attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments[1].format         = stencilFormat;
        attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, stencil { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass { };
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &color;
        subpass.pDepthStencilAttachment = &stencil;
        VkSubpassDependency dependency { };
        dependency.srcSubpass   = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass   = 0;
        dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask                          = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        pass.attachmentCount = 2;
        pass.pAttachments    = attachments;
        pass.subpassCount    = 1;
        pass.pSubpasses      = &subpass;
        pass.dependencyCount = 1;
        pass.pDependencies   = &dependency;
        check(vkCreateRenderPass(device, &pass, nullptr, &renderPass), "render pass");
    }
    VkSwapchainCreateInfoKHR info { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    info.surface       = surface;
    info.minImageCount = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount)
        info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
    info.imageFormat      = format;
    info.imageColorSpace  = selected->colorSpace;
    info.imageExtent      = extent;
    info.imageArrayLayers = 1;
    info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.preTransform     = caps.currentTransform;
    info.compositeAlpha   = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VkCompositeAlphaFlagBitsKHR(caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1u));
    info.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped          = VK_TRUE;
    check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "swapchain");
    check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "swapchain image count");
    std::vector<VkImage> images(count);
    check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()), "swapchain images");
    targets.resize(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        auto& target = targets[i];
        target.image = images[i];
        target.view  = imageView(device, target.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
        VkImageCreateInfo depth { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        depth.imageType = VK_IMAGE_TYPE_2D;
        depth.format    = stencilFormat;
        depth.extent    = { extent.width, extent.height, 1 };
        depth.mipLevels = depth.arrayLayers = 1;
        depth.samples                       = VK_SAMPLE_COUNT_1_BIT;
        depth.tiling                        = VK_IMAGE_TILING_OPTIMAL;
        depth.usage                         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        check(vkCreateImage(device, &depth, nullptr, &target.stencil), "stencil image");
        VkMemoryRequirements requirements { };
        vkGetImageMemoryRequirements(device, target.stencil, &requirements);
        VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize  = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, 0);
        check(vkAllocateMemory(device, &allocation, nullptr, &target.stencilMemory), "stencil memory");
        check(vkBindImageMemory(device, target.stencil, target.stencilMemory, 0), "bind stencil");
        target.stencilView = imageView(device, target.stencil, stencilFormat, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        VkImageView views[] { target.view, target.stencilView };
        VkFramebufferCreateInfo framebuffer { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebuffer.renderPass      = renderPass;
        framebuffer.attachmentCount = 2;
        framebuffer.pAttachments    = views;
        framebuffer.width           = extent.width;
        framebuffer.height          = extent.height;
        framebuffer.layers          = 1;
        check(vkCreateFramebuffer(device, &framebuffer, nullptr, &target.framebuffer), "framebuffer");
        VkSemaphoreCreateInfo semaphore { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        check(vkCreateSemaphore(device, &semaphore, nullptr, &target.complete), "present semaphore");
    }
    resizePending = false;
    brls::Logger::info("SWITCH2_VULKAN: swapchain {}x{} images={} native usage=0x{:x}", extent.width, extent.height, count, info.imageUsage);
}

void Switch2VulkanContext::begin()
{
    u32 w = extent.width, h = extent.height;
    nwindowGetDimensions(nwindowGetDefault(), &w, &h);
    if (resizePending || w != extent.width || h != extent.height)
        recreate();
    auto& flight = flights[frameIndex];
    check(vkWaitForFences(device, 1, &flight.fence, VK_TRUE, UINT64_MAX), "frame completion");
    release(flight);
    VkResult rc = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, flight.acquired, VK_NULL_HANDLE, &imageIndex);
    if (rc == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreate();
        rc = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, flight.acquired, VK_NULL_HANDLE, &imageIndex);
    }
    if (rc == VK_SUBOPTIMAL_KHR)
        resizePending = true;
    else
        check(rc, "acquire");
    check(vkResetCommandBuffer(command(), 0), "reset commands");
    VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command(), &begin), "begin commands");
    // Native GetFrame waits for its producer before a frame is queued. The
    // imported storage is written outside Vulkan; invalidate shader caches at
    // execution time, outside the render pass, before any direct video reads.
    VkMemoryBarrier externalWrites { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    externalWrites.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    externalWrites.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        1, &externalWrites, 0, nullptr, 0, nullptr);
    VkClearValue values[2] { };
    values[0].color.float32[3] = 1;
    values[1].depthStencil     = { 1, 0 };
    VkRenderPassBeginInfo pass { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    pass.renderPass        = renderPass;
    pass.framebuffer       = targets[imageIndex].framebuffer;
    pass.renderArea.extent = extent;
    pass.clearValueCount   = 2;
    pass.pClearValues      = values;
    vkCmdBeginRenderPass(command(), &pass, VK_SUBPASS_CONTENTS_INLINE);
    // NanoVG pipelines use dynamic viewport/scissor and expect the host to set
    // them. Initialize these even when no streamed video is being drawn.
    VkViewport viewport { 0, 0, float(extent.width), float(extent.height), 0, 1 };
    VkRect2D scissor { { 0, 0 }, extent };
    vkCmdSetViewport(command(), 0, 1, &viewport);
    vkCmdSetScissor(command(), 0, 1, &scissor);
}
void Switch2VulkanContext::clear(NVGcolor color)
{
    VkClearAttachment attachment { };
    attachment.aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.clearValue.color = { { color.r, color.g, color.b, color.a } };
    VkClearRect rect { };
    rect.rect.extent = extent;
    rect.layerCount  = 1;
    vkCmdClearAttachments(command(), 1, &attachment, 1, &rect);
}
void Switch2VulkanContext::end()
{
    vkCmdEndRenderPass(command());
    check(vkEndCommandBuffer(command()), "end commands");
    auto& flight              = flights[frameIndex];
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &flight.acquired;
    submit.pWaitDstStageMask    = &wait;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &commands[frameIndex];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &targets[imageIndex].complete;
    std::lock_guard<std::mutex> lock(queueMutex);
    check(vkResetFences(device, 1, &flight.fence), "reset fence");
    check(vkQueueSubmit(queue, 1, &submit, flight.fence), "submit");
    VkPresentInfoKHR present { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &targets[imageIndex].complete;
    present.swapchainCount     = 1;
    present.pSwapchains        = &swapchain;
    present.pImageIndices      = &imageIndex;
    const auto rc              = vkQueuePresentKHR(queue, &present);
    if (rc == VK_SUBOPTIMAL_KHR || rc == VK_ERROR_OUT_OF_DATE_KHR)
        resizePending = true;
    else
        check(rc, "present");
    frameIndex = (frameIndex + 1) % flights.size();
}

Switch2VulkanContext::~Switch2VulkanContext()
{
    if (!device)
        return;
    vkDeviceWaitIdle(device);
    for (auto& flight : flights)
        release(flight);
    if (vg)
        nvgDeleteVk(vg);
    destroyTargets();
    if (renderPass)
        vkDestroyRenderPass(device, renderPass, nullptr);
    for (const auto& flight : flights)
    {
        vkDestroyFence(device, flight.fence, nullptr);
        vkDestroySemaphore(device, flight.acquired, nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);
    active = nullptr;
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}

SDLVideoContext::SDLVideoContext(std::string title, uint32_t, uint32_t, float, float)
{
    SDL_SetHint("SDL_SWITCH_NATIVE_WINDOW", "1");
    u32 w = 1920, h = 1080;
    nwindowSetModeDimensions(nwindowGetDefault(), 0, 0, 0, 0);
    nwindowGetDimensions(nwindowGetDefault(), &w, &h);
    window = SDL_CreateWindow(title.c_str(), w, h, SDL_WINDOW_RESIZABLE);
    if (!window)
        throw std::runtime_error(SDL_GetError());
    context    = std::make_unique<Switch2VulkanContext>();
    nvgContext = context->vg;
    Application::setWindowSize(context->extent.width, context->extent.height);
}
SDLVideoContext::~SDLVideoContext()
{
    context.reset();
    nvgContext = nullptr;
    if (window)
        SDL_DestroyWindow(window);
}
NVGcontext* SDLVideoContext::getNVGContext() { return nvgContext; }
SDL_Window* SDLVideoContext::getSDLWindow() { return window; }
double SDLVideoContext::getScaleFactor() { return 1.0; }
void SDLVideoContext::fullScreen(bool) { }
void SDLVideoContext::resetState() { }
void SDLVideoContext::setSwapInterval(int interval)
{
    VideoContext::swapInterval = interval;
    nwindowSetFrameRates(nwindowGetDefault(), 120, 60, true);
}
void SDLVideoContext::beginFrame()
{
    context->begin();
    if (Application::windowWidth != context->extent.width || Application::windowHeight != context->extent.height)
        Application::setWindowSize(context->extent.width, context->extent.height);
}
void SDLVideoContext::clear(NVGcolor color) { context->clear(color); }
void SDLVideoContext::endFrame() { context->end(); }
}
#endif
