#include "RHI/UploadRing.h"

#include "RHI/Device.h"

#include <algorithm>
#include <cstring>

namespace rhi {

void UploadRing::create(Device& device, VkDeviceSize capacity,
                        const char* debugName) {
    destroy();
    device_ = &device;
    capacity_ = capacity;

    BufferDesc desc{};
    desc.size = capacity;
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
    inFlight_.clear();
}

void UploadRing::rollback(const Marker& m) {
    headV_ = m.headV;
    pending_.resize(m.pendingCount);
}

bool UploadRing::stage(const Buffer& dst, VkDeviceSize dstOffset,
                       const void* data, VkDeviceSize size) {
    if (!valid() || size == 0 || size > capacity_)
        return false;

    // Never straddle the physical wrap: pad the tail run away.
    uint64_t alloc = headV_;
    const VkDeviceSize phys = static_cast<VkDeviceSize>(alloc % capacity_);
    if (phys + size > capacity_)
        alloc += capacity_ - phys;
    if (alloc + size - tailV_ > capacity_)
        return false; // full — caller retries after reclaim()
    headV_ = alloc + size;

    const VkDeviceSize srcOffset = static_cast<VkDeviceSize>(alloc % capacity_);
    std::memcpy(static_cast<char*>(buffer_.mapped()) + srcOffset, data, size);
    pending_.push_back({ dst.handle(), srcOffset, dstOffset, size });
    return true;
}

void UploadRing::flush(VkCommandBuffer cmd) {
    if (pending_.empty())
        return;

    // Non-coherent host memory: make the staged writes visible to the copies
    // (VMA no-ops these when the memory is HOST_COHERENT).
    for (const PendingCopy& c : pending_)
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

    // One coarse barrier covers every destination: transfer writes -> any
    // later access this frame. Precise per-buffer tracking buys nothing here
    // — the flush happens once, before all scene work.
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

} // namespace rhi
