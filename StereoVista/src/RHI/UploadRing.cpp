#include "RHI/UploadRing.h"

#include "RHI/Barrier.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"

#include <algorithm>
#include <cstring>

namespace rhi {

void UploadRing::create(Device& device, VkDeviceSize capacity,
                        const char* debugName) {
    destroy();
    device_ = &device;
    // allocSpan's physical alignment relies on capacity being a multiple of
    // every alignment it hands out (all <= 256).
    capacity_ = (capacity + 255) & ~VkDeviceSize(255);

    BufferDesc desc{};
    desc.size = capacity_;
    desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    desc.memory = MemoryUsage::HostUpload;
    desc.debugName = debugName;
    buffer_.create(device, desc);
}

void UploadRing::destroy() {
    buffer_.destroy();
    device_ = nullptr;
    capacity_ = 0;
    headV_ = tailV_ = flushedHeadV_ = 0;
    flushDirty_ = false;
    pending_.clear();
    pendingImages_.clear();
    inFlight_.clear();
}

void UploadRing::rollback(const Marker& m) {
    headV_ = m.headV;
    pending_.resize(m.pendingCount);
    pendingImages_.resize(m.pendingImageCount);
}

bool UploadRing::allocSpan(VkDeviceSize size, VkDeviceSize align,
                           VkDeviceSize& outSrcOffset) {
    if (!valid() || size == 0 || size > capacity_)
        return false;

    // Align the virtual offset (capacity is a power of two, so the physical
    // offset aligns with it), then never straddle the physical wrap: pad the
    // tail run away.
    uint64_t alloc = (headV_ + align - 1) & ~(uint64_t(align) - 1);
    const VkDeviceSize phys = static_cast<VkDeviceSize>(alloc % capacity_);
    if (phys + size > capacity_)
        alloc += capacity_ - phys;
    if (alloc + size - tailV_ > capacity_)
        return false; // full — caller retries after reclaim()
    headV_ = alloc + size;
    outSrcOffset = static_cast<VkDeviceSize>(alloc % capacity_);
    return true;
}

bool UploadRing::stage(const Buffer& dst, VkDeviceSize dstOffset,
                       const void* data, VkDeviceSize size) {
    VkDeviceSize srcOffset = 0;
    if (!allocSpan(size, 1, srcOffset))
        return false;
    std::memcpy(static_cast<char*>(buffer_.mapped()) + srcOffset, data, size);
    pending_.push_back({ dst.handle(), srcOffset, dstOffset, size });
    return true;
}

bool UploadRing::stageImage(const Texture& dst, uint32_t mipLevel,
                            const void* data, VkDeviceSize size) {
    // bufferOffset for a buffer->image copy must be a multiple of the texel
    // block size; 16 covers every accepted format (RGBA8 = 4, BC7 = 16).
    VkDeviceSize srcOffset = 0;
    if (!allocSpan(size, 16, srcOffset))
        return false;
    std::memcpy(static_cast<char*>(buffer_.mapped()) + srcOffset, data, size);
    const VkExtent2D extent = { std::max(dst.extent().width >> mipLevel, 1u),
                                std::max(dst.extent().height >> mipLevel, 1u) };
    pendingImages_.push_back({ dst.image(), srcOffset, size, mipLevel, extent });
    return true;
}

void UploadRing::flush(VkCommandBuffer cmd) {
    if (pending_.empty() && pendingImages_.empty())
        return;

    // Non-coherent host memory: make the staged writes visible to the copies
    // (VMA no-ops these when the memory is HOST_COHERENT).
    for (const PendingCopy& c : pending_)
        buffer_.flush(c.srcOffset, c.size);
    for (const PendingImageCopy& c : pendingImages_)
        buffer_.flush(c.srcOffset, c.size);

    // One vkCmdCopyBuffer2 per consecutive run of same-destination copies
    // (a stream chunk's five section copies collapse into one call).
    std::vector<VkBufferCopy2> regions;
    size_t i = 0;
    while (i < pending_.size()) {
        const VkBuffer dst = pending_[i].dst;
        regions.clear();
        while (i < pending_.size() && pending_[i].dst == dst) {
            VkBufferCopy2 region{};
            region.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            region.srcOffset = pending_[i].srcOffset;
            region.dstOffset = pending_[i].dstOffset;
            region.size = pending_[i].size;
            regions.push_back(region);
            ++i;
        }
        VkCopyBufferInfo2 copy{};
        copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copy.srcBuffer = buffer_.handle();
        copy.dstBuffer = dst;
        copy.regionCount = static_cast<uint32_t>(regions.size());
        copy.pRegions = regions.data();
        vkCmdCopyBuffer2(cmd, &copy);
    }
    pending_.clear();

    // Image copies (M2, plan §6.4). Contract (stageImage): each destination
    // image had its FULL mip chain staged since the last flush, is
    // single-layer color, and is not otherwise in use — so this flush owns
    // its whole lifecycle: UNDEFINED -> TRANSFER_DST, copy every level,
    // -> SHADER_READ_ONLY. One image's mips are consecutive in the queue
    // (staged under one transaction), so runs group exactly like buffers.
    if (!pendingImages_.empty()) {
        // All acquire barriers first, in one dependency.
        std::vector<VkImageMemoryBarrier2> barriers;
        for (size_t b = 0; b < pendingImages_.size(); ++b) {
            if (b > 0 && pendingImages_[b].dst == pendingImages_[b - 1].dst)
                continue;
            barriers.push_back(imageBarrier(
                pendingImages_[b].dst, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
        }
        cmdBarrier(cmd, barriers.data(), static_cast<uint32_t>(barriers.size()));

        std::vector<VkBufferImageCopy2> imageRegions;
        size_t j = 0;
        while (j < pendingImages_.size()) {
            const VkImage dst = pendingImages_[j].dst;
            imageRegions.clear();
            while (j < pendingImages_.size() && pendingImages_[j].dst == dst) {
                const PendingImageCopy& c = pendingImages_[j];
                VkBufferImageCopy2 region{};
                region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
                region.bufferOffset = c.srcOffset;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = c.mipLevel;
                region.imageSubresource.layerCount = 1;
                region.imageExtent = { c.extent.width, c.extent.height, 1 };
                imageRegions.push_back(region);
                ++j;
            }
            VkCopyBufferToImageInfo2 copy{};
            copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
            copy.srcBuffer = buffer_.handle();
            copy.dstImage = dst;
            copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copy.regionCount = static_cast<uint32_t>(imageRegions.size());
            copy.pRegions = imageRegions.data();
            vkCmdCopyBufferToImage2(cmd, &copy);
        }

        // Release every image to sampling, again in one dependency.
        for (VkImageMemoryBarrier2& barrier : barriers) {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        cmdBarrier(cmd, barriers.data(), static_cast<uint32_t>(barriers.size()));
        pendingImages_.clear();
    }

    // One coarse barrier covers every buffer destination: transfer writes ->
    // any later access this frame. Precise per-buffer tracking buys nothing
    // here — the flush happens once, before all scene work.
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT |
                            VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    flushedHeadV_ = headV_;
    flushDirty_ = true;
}

void UploadRing::notifySubmitted(uint64_t timelineValue) {
    if (!flushDirty_)
        return;
    inFlight_.push_back({ flushedHeadV_, timelineValue });
    flushDirty_ = false;
}

void UploadRing::reclaim(uint64_t completedTimelineValue) {
    while (!inFlight_.empty() &&
           inFlight_.front().timelineValue <= completedTimelineValue) {
        tailV_ = inFlight_.front().headV;
        inFlight_.pop_front();
    }
}

void UploadRing::dropPendingFor(const Buffer& dst) {
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [&](const PendingCopy& c) {
                                      return c.dst == dst.handle();
                                  }),
                   pending_.end());
    // The ring space those copies held stays allocated until the surrounding
    // flushes retire — wasted bytes for a frame or two, never a hazard.
}

void UploadRing::dropPendingFor(const Texture& dst) {
    pendingImages_.erase(std::remove_if(pendingImages_.begin(),
                                        pendingImages_.end(),
                                        [&](const PendingImageCopy& c) {
                                            return c.dst == dst.image();
                                        }),
                         pendingImages_.end());
}

} // namespace rhi
