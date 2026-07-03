#include "Renderer/PointCloudGpu.h"

#include "RHI/Device.h"
#include "RHI/UploadRing.h"

#include <exception>
#include <iostream>

namespace renderer {

namespace {
VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
} // namespace

bool PointCloudGpu::create(rhi::Device& device, rhi::UploadRing* ring,
                           uint32_t maxPoints, uint32_t maxBatches,
                           const std::string& name) {
    destroy();
    if (maxPoints == 0 || maxBatches == 0)
        return false;

    device_ = &device;
    ring_ = ring;
    capacityPoints = maxPoints;
    capacityBatches = maxBatches;

    const VkDeviceSize batchBytes = VkDeviceSize(maxBatches) * kBatchStride;
    const VkDeviceSize streamBytes = VkDeviceSize(maxPoints) * sizeof(uint32_t);

    batchesOffset = 0;
    xyz4bOffset = alignUp(batchBytes, kSectionAlign);
    xyz8bOffset = xyz4bOffset + alignUp(streamBytes, kSectionAlign);
    xyz12bOffset = xyz8bOffset + alignUp(streamBytes, kSectionAlign);
    rgbaOffset = xyz12bOffset + alignUp(streamBytes, kSectionAlign);
    const VkDeviceSize totalBytes = rgbaOffset + streamBytes;

    rhi::BufferDesc desc{};
    desc.size = totalBytes;
    // TRANSFER_DST is implied for GpuOnly; TRANSFER_SRC serves the export
    // readback (forEachPointBatch); DEVICE_ADDRESS is the Phase 5 access path.
    desc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    desc.memory = rhi::MemoryUsage::GpuOnly;
    desc.dedicated = totalBytes >= kDedicatedBytes;
    const std::string debugName = "pointcloud '" + name + "'";
    desc.debugName = debugName.c_str();

    try {
        storage.create(device, desc);
    } catch (const std::exception& e) {
        std::cerr << "[PointCloudGpu] allocation of "
                  << (totalBytes >> 20) << " MB failed for '" << name
                  << "' — cloud likely exceeds available VRAM (" << e.what()
                  << ")\n";
        destroy();
        return false;
    }

    const VkDeviceAddress base = storage.deviceAddress();
    addresses.batches = base + batchesOffset;
    addresses.xyz4b = base + xyz4bOffset;
    addresses.xyz8b = base + xyz8bOffset;
    addresses.xyz12b = base + xyz12bOffset;
    addresses.rgba = base + rgbaOffset;
    return true;
}

void PointCloudGpu::destroy() {
    if (storage.valid()) {
        if (ring_)
            ring_->dropPendingFor(storage);
        // Flushed ring copies (and, from Phase 5 on, draws) may still touch
        // the buffer. Unloading a cloud is a rare user action — waiting for
        // the device is the simple correct answer today.
        if (device_)
            device_->waitIdle();
        storage.destroy();
    }
    device_ = nullptr;
    ring_ = nullptr;
    batchesOffset = xyz4bOffset = xyz8bOffset = xyz12bOffset = rgbaOffset = 0;
    addresses = {};
    capacityPoints = 0;
    capacityBatches = 0;
}

} // namespace renderer
