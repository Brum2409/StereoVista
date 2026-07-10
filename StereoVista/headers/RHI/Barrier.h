#pragma once

#include "RHI/VulkanCommon.h"

namespace rhi {

// synchronization2 barrier helpers shared by the renderer, upload paths and
// readback code. These only assemble the structs; recording happens through
// cmdBarrier so every barrier in the app takes the same path.

inline VkImageMemoryBarrier2 imageBarrier(
    VkImage image, VkImageAspectFlags aspect,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    uint32_t baseMip = 0, uint32_t mipCount = VK_REMAINING_MIP_LEVELS,
    uint32_t baseLayer = 0, uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = mipCount;
    barrier.subresourceRange.baseArrayLayer = baseLayer;
    barrier.subresourceRange.layerCount = layerCount;
    return barrier;
}

inline VkBufferMemoryBarrier2 bufferBarrier(
    VkBuffer buffer,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
    VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE) {
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = offset;
    barrier.size = size;
    return barrier;
}

// ---- Queue-family ownership transfer (async compute) ----
// EXCLUSIVE resources whose contents must survive a graphics <-> compute
// family hop need a paired transfer: record the RELEASE barrier on the source
// queue, the ACQUIRE barrier (identical parameters) on the destination queue,
// and order the two submissions with a semaphore. Per the spec, the release's
// dstStageMask/dstAccessMask and the acquire's srcStageMask/srcAccessMask are
// ignored — both halves come from ONE description of the transfer.
//
// Buffers that hop EVERY frame don't use this: create them with
// BufferDesc::shareGraphicsCompute (CONCURRENT) instead. These helpers exist
// for resources where exclusive ownership is worth keeping — images (where
// CONCURRENT disables compression) and rarely-migrating buffers (e.g. a
// future BLAS/TLAS handed from a compute-queue build to graphics-queue use).

inline VkBufferMemoryBarrier2 bufferQueueTransfer(
    VkBuffer buffer, uint32_t srcQueueFamily, uint32_t dstQueueFamily,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
    VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE) {
    VkBufferMemoryBarrier2 barrier =
        bufferBarrier(buffer, srcStage, srcAccess, dstStage, dstAccess, offset, size);
    barrier.srcQueueFamilyIndex = srcQueueFamily;
    barrier.dstQueueFamilyIndex = dstQueueFamily;
    return barrier;
}

inline VkImageMemoryBarrier2 imageQueueTransfer(
    VkImage image, VkImageAspectFlags aspect,
    uint32_t srcQueueFamily, uint32_t dstQueueFamily,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
    VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier2 barrier =
        imageBarrier(image, aspect, srcStage, srcAccess, dstStage, dstAccess,
                     oldLayout, newLayout);
    barrier.srcQueueFamilyIndex = srcQueueFamily;
    barrier.dstQueueFamilyIndex = dstQueueFamily;
    return barrier;
}

inline void cmdBarrier(VkCommandBuffer cmd,
                       const VkImageMemoryBarrier2* images, uint32_t imageCount,
                       const VkBufferMemoryBarrier2* buffers = nullptr,
                       uint32_t bufferCount = 0) {
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = imageCount;
    dep.pImageMemoryBarriers = images;
    dep.bufferMemoryBarrierCount = bufferCount;
    dep.pBufferMemoryBarriers = buffers;
    vkCmdPipelineBarrier2(cmd, &dep);
}

inline void cmdImageBarrier(VkCommandBuffer cmd, const VkImageMemoryBarrier2& barrier) {
    cmdBarrier(cmd, &barrier, 1);
}

} // namespace rhi
