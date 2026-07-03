#pragma once

#include "RHI/Buffer.h"

#include <cstdint>
#include <string>

namespace rhi {
class Device;
class UploadRing;
}

namespace renderer {

// GPU addresses of the five cloud sections, in the shape the Phase 5 compute
// rasterizer consumes: ONE push-constant struct of buffer_reference pointers
// per cloud (playbook A.2) instead of the GL path's five SSBO binds per cloud
// per pass.
struct PointCloudGpuAddresses {
    VkDeviceAddress batches = 0; // ComputeBatch[numBatches]
    VkDeviceAddress xyz4b = 0;   // uint[points] — coarsest 10 bits/axis
    VkDeviceAddress xyz8b = 0;   // uint[points] — middle 10 bits/axis
    VkDeviceAddress xyz12b = 0;  // uint[points] — finest 10 bits/axis
    VkDeviceAddress rgba = 0;    // uint[points] — packed RGBA (A = intensity)
};

// GPU residency of one Schütz compute cloud. Improvement over the GL design:
// the five separate SSBOs become ONE device-local buffer with 256-aligned
// sections — one allocation (dedicated above kDedicatedBytes so huge clouds
// bypass the VMA pools, playbook A.13), one destruction, one barrier target,
// five BDA section pointers. Capacity is fixed at create(); the streaming
// loader pre-allocates it from the file header exactly like the GL path
// (GL_ARB_sparse_buffer on-demand commitment was dropped: VRAM now tracks the
// full reservation, the pre-Vulkan behaviour on every non-sparse driver).
//
// Section layout: [batches][xyz4b][xyz8b][xyz12b][rgba].
struct PointCloudGpu {
    PointCloudGpu() = default;
    ~PointCloudGpu() { destroy(); }
    PointCloudGpu(const PointCloudGpu&) = delete;
    PointCloudGpu& operator=(const PointCloudGpu&) = delete;

    // False (with a console message) when the allocation fails — the caller
    // reports the cloud as not loaded, matching the GL VRAM-exhaustion path.
    // `ring` may be null (bulk loads that never stream); when set, destroy()
    // drops the cloud's queued ring copies first.
    bool create(rhi::Device& device, rhi::UploadRing* ring,
                uint32_t maxPoints, uint32_t maxBatches, const std::string& name);

    // Safe on a live cloud: drops queued ring copies, then waits for the
    // device before freeing (unload is a user action; the stall is fine).
    // TODO(Phase 5): timeline-deferred destruction once clouds render.
    void destroy();

    bool valid() const { return storage.valid(); }

    rhi::Buffer storage; // the one suballocated buffer holding all sections

    // Byte offsets of each section inside `storage` (upload destinations).
    VkDeviceSize batchesOffset = 0;
    VkDeviceSize xyz4bOffset = 0;
    VkDeviceSize xyz8bOffset = 0;
    VkDeviceSize xyz12bOffset = 0;
    VkDeviceSize rgbaOffset = 0;

    PointCloudGpuAddresses addresses; // storage BDA + section offsets

    uint32_t capacityPoints = 0;
    uint32_t capacityBatches = 0;

    static constexpr VkDeviceSize kSectionAlign = 256;  // covers every
        // minStorageBufferOffsetAlignment, should a descriptor bind ever be
        // preferred over BDA for a section
    static constexpr VkDeviceSize kDedicatedBytes = 64ull << 20;
    // sizeof(Engine::ComputeBatch); static_asserted at the loader, kept as a
    // constant here so this header stays below the Engine layer.
    static constexpr VkDeviceSize kBatchStride = 32;

private:
    rhi::Device* device_ = nullptr;
    rhi::UploadRing* ring_ = nullptr;
};

} // namespace renderer
