#ifndef NANOVG_VK_H
#define NANOVG_VK_H

#include <stdbool.h>
#include <vulkan/vulkan.h>
#include "nanovg.h"

#ifdef __cplusplus
extern "C" {
#endif

enum NVGcreateFlags {
  NVG_ANTIALIAS = 1 << 0,
  NVG_STENCIL_STROKES = 1 << 1,
  NVG_DEBUG = 1 << 2,
};

typedef struct VkNvgExt {
  bool dynamicState;
  bool colorBlendEquation;
  bool colorWriteMask;
} VkNvgExt;

typedef struct VKNVGCreateInfo {
  VkPhysicalDevice gpu;
  VkDevice device;
  VkRenderPass renderpass;
  VkCommandBuffer *cmdBuffer; /* length = frameCount (or swapchainImageCount if frameCount is 0) */
  uint32_t swapchainImageCount; /* used as frames-in-flight when frameCount is 0 */
  uint32_t frameCount; /* optional; 0 means use swapchainImageCount */
  uint32_t *currentFrame; /* in-flight index, 0 .. frameCount-1 */
  VkCommandPool commandPool; /* optional; texture uploads. If VK_NULL_HANDLE, a pool is created using graphicsQueueFamilyIndex */
  uint32_t graphicsQueueFamilyIndex; /* used when commandPool is VK_NULL_HANDLE (0 is a valid family) */
  const VkAllocationCallbacks *allocator;
  VkNvgExt ext;
} VKNVGCreateInfo;

/* createInfo is copied; cmdBuffer / currentFrame pointers must remain valid for the context lifetime. */
NVGcontext *nvgCreateVk(const VKNVGCreateInfo *createInfo, int flags, VkQueue queue);
void nvgDeleteVk(NVGcontext *ctx);
/* Application-owned VkImage. NanoVG does not create or destroy image, view, sampler, or memory. */
int nvgCreateImageFromHandleVk(NVGcontext *ctx, VkImage image, VkImageView view, VkSampler sampler, VkImageLayout layout, int w, int h, int imageFlags);

#ifdef __cplusplus
}
#endif

#endif
