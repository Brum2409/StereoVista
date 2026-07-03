#pragma once

#include "RHI/VulkanCommon.h"
#include "RHI/Vma.h"

namespace rhi {

class Device;

// Who touches the memory. VMA picks the actual memory type; these express
// intent, which is all the callers should have to know.
enum class MemoryUsage {
    GpuOnly,      // device-local; filled through upload() staging or GPU writes
    HostUpload,   // persistently mapped, CPU write / GPU read (UBOs, staging,
                  //   per-frame streams)
    HostReadback, // persistently mapped, GPU write / CPU read (screenshots,
                  //   queries)
};

struct BufferDesc {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0; // TRANSFER_DST is added automatically for
                                  // GpuOnly so upload() always works
    MemoryUsage memory = MemoryUsage::GpuOnly;
    const char* debugName = nullptr;
};

// VMA-backed VkBuffer. Move-only RAII; destroy() is idempotent and safe on a
// default-constructed instance.
class Buffer {
public:
    Buffer() = default;
    ~Buffer() { destroy(); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept { moveFrom(other); }
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            destroy();
            moveFrom(other);
        }
        return *this;
    }

    void create(Device& device, const BufferDesc& desc);
    void destroy();

    // Copies CPU data into the buffer. Host-visible buffers: memcpy + flush.
    // GpuOnly buffers: staging buffer + copy on Device::immediateSubmit —
    // setup/loading work only; the per-frame streaming path (Phase 4) rides a
    // persistent ring instead.
    void upload(const void* data, VkDeviceSize size, VkDeviceSize dstOffset = 0);

    // Explicit cache maintenance for HostUpload/HostReadback memory that is
    // not HOST_COHERENT (VMA no-ops these when it is).
    void flush(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    void invalidate(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

    VkBuffer handle() const { return buffer_; }
    VkDeviceSize size() const { return size_; }
    bool valid() const { return buffer_ != VK_NULL_HANDLE; }

    // Persistent mapping; null for GpuOnly.
    void* mapped() const { return mapped_; }

    // Requires VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT in the desc.
    VkDeviceAddress deviceAddress() const;

private:
    void moveFrom(Buffer& other);

    Device* device_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
    VkBufferUsageFlags usage_ = 0;
};

} // namespace rhi
