#pragma once

#include "RHI/Buffer.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace rhi {

class Device;

// Persistently-mapped staging ring for per-frame GPU uploads (playbook A.13).
// Streaming producers stage small CPU chunks at any point of the frame; the
// Renderer records every queued copy in ONE batch at the top of the frame
// command buffer and retires ring space against the frame timeline. This
// replaces the GL path's per-SSBO glBufferSubData (driver-managed staging +
// hidden synchronization) with explicit reuse of one host-visible allocation
// — no per-upload buffer creation, no submit-and-wait.
//
// Threading: main/render thread only. Loader worker threads never touch the
// ring — they hand CPU chunks over (PointCloudStream) and the main-thread
// pump (PointCloudLoader::updateStreaming) stages them.
//
// Space accounting uses monotonically increasing virtual offsets (physical =
// virtual % capacity); an allocation never straddles the wrap point (the
// tail run is padded away). A full ring makes stage() return false — callers
// retry next frame, which naturally backpressures the producer.
class UploadRing {
public:
    UploadRing() = default;
    ~UploadRing() { destroy(); }
    UploadRing(const UploadRing&) = delete;
    UploadRing& operator=(const UploadRing&) = delete;

    void create(Device& device, VkDeviceSize capacity, const char* debugName);
    void destroy();
    bool valid() const { return buffer_.valid(); }
    VkDeviceSize capacity() const { return capacity_; }

    // Transaction marker: mark() before staging a multi-copy unit (e.g. one
    // stream chunk = five copies), rollback() if any stage() fails, so the
    // unit stays all-or-nothing and can be retried next frame.
    struct Marker {
        uint64_t headV = 0;
        size_t pendingCount = 0;
    };
    Marker mark() const { return { headV_, pending_.size() }; }
    void rollback(const Marker& m);

    // Copies `size` bytes into ring memory and queues a GPU copy into dst at
    // dstOffset. False = the ring cannot fit the request right now (nothing
    // was staged); retry after reclaim() frees space.
    bool stage(const Buffer& dst, VkDeviceSize dstOffset, const void* data,
               VkDeviceSize size);

    // Records all queued copies (regions batched per destination buffer) plus
    // one transfer->everything barrier. Called once per frame by the Renderer
    // at the top of the frame command buffer; no-op when nothing is staged.
    void flush(VkCommandBuffer cmd);

    // The most recent flush() rides the submission that signals the frame
    // timeline with `timelineValue`; its ring space frees on reclaim().
    void notifySubmitted(uint64_t timelineValue);

    // Returns ring space whose submission has completed (frame timeline
    // counter >= the value given at notifySubmitted). Called once per frame.
    void reclaim(uint64_t completedTimelineValue);

    // Drops queued-but-unflushed copies targeting dst (call before destroying
    // a destination buffer). Copies already flushed may still execute — the
    // caller must ensure the GPU is done with dst (timeline wait / waitIdle).
    void dropPendingFor(const Buffer& dst);

    // Diagnostics for the debug panel.
    VkDeviceSize usedBytes() const {
        return static_cast<VkDeviceSize>(headV_ - tailV_);
    }
    uint32_t pendingCopies() const { return static_cast<uint32_t>(pending_.size()); }

private:
    struct PendingCopy {
        VkBuffer dst = VK_NULL_HANDLE;
        VkDeviceSize srcOffset = 0;
        VkDeviceSize dstOffset = 0;
        VkDeviceSize size = 0;
    };
    struct InFlight {
        uint64_t headV = 0;         // the ring is free up to here once retired
        uint64_t timelineValue = 0; // frame timeline value that retires it
    };

    Device* device_ = nullptr;
    Buffer buffer_;
    VkDeviceSize capacity_ = 0;
    uint64_t headV_ = 0;        // next virtual write offset
    uint64_t tailV_ = 0;        // oldest virtual byte still owned by the GPU
    uint64_t flushedHeadV_ = 0; // headV at the last flush(), awaiting submit
    bool flushDirty_ = false;   // flush() happened since last notifySubmitted
    std::vector<PendingCopy> pending_;
    std::deque<InFlight> inFlight_;
};

} // namespace rhi
