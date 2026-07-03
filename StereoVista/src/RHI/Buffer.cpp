#include "RHI/Buffer.h"

#include "RHI/Device.h"

#include <cstring>
#include <stdexcept>

namespace rhi {

void Buffer::create(Device& device, const BufferDesc& desc) {
    destroy();
    if (desc.size == 0)
        throw std::runtime_error("rhi::Buffer: zero-sized buffer requested");

    device_ = &device;
    size_ = desc.size;
    usage_ = desc.usage;
    if (desc.memory == MemoryUsage::GpuOnly)
        usage_ |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size_;
    bufInfo.usage = usage_;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    switch (desc.memory) {
    case MemoryUsage::GpuOnly:
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case MemoryUsage::HostUpload:
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryUsage::HostReadback:
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }

    VmaAllocationInfo outInfo{};
    VK_CHECK(vmaCreateBuffer(device_->allocator(), &bufInfo, &allocInfo, &buffer_,
                             &allocation_, &outInfo));
    mapped_ = outInfo.pMappedData;

    if (desc.debugName)
        device_->setDebugName(VK_OBJECT_TYPE_BUFFER,
                              reinterpret_cast<uint64_t>(buffer_), desc.debugName);
}

void Buffer::upload(const void* data, VkDeviceSize size, VkDeviceSize dstOffset) {
    if (!valid())
        throw std::runtime_error("rhi::Buffer::upload on an invalid buffer");
    if (dstOffset + size > size_)
        throw std::runtime_error("rhi::Buffer::upload out of range");

    if (mapped_) {
        std::memcpy(static_cast<char*>(mapped_) + dstOffset, data, size);
        flush(dstOffset, size);
        return;
    }

    Buffer staging;
    BufferDesc stagingDesc{};
    stagingDesc.size = size;
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.memory = MemoryUsage::HostUpload;
    staging.create(*device_, stagingDesc);
    std::memcpy(staging.mapped(), data, size);
    staging.flush();

    device_->immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        region.size = size;
        region.dstOffset = dstOffset;
        VkCopyBufferInfo2 copy{};
        copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copy.srcBuffer = staging.handle();
        copy.dstBuffer = buffer_;
        copy.regionCount = 1;
        copy.pRegions = &region;
        vkCmdCopyBuffer2(cmd, &copy);
    });
    // immediateSubmit waits for the queue, so the staging buffer can die here
    // and later submits need no extra barrier against this copy.
}

void Buffer::flush(VkDeviceSize offset, VkDeviceSize size) {
    if (allocation_ != VK_NULL_HANDLE)
        VK_CHECK(vmaFlushAllocation(device_->allocator(), allocation_, offset, size));
}

void Buffer::invalidate(VkDeviceSize offset, VkDeviceSize size) {
    if (allocation_ != VK_NULL_HANDLE)
        VK_CHECK(vmaInvalidateAllocation(device_->allocator(), allocation_, offset, size));
}

VkDeviceAddress Buffer::deviceAddress() const {
    if (!(usage_ & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
        throw std::runtime_error(
            "rhi::Buffer::deviceAddress requires SHADER_DEVICE_ADDRESS usage");
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer_;
    return vkGetBufferDeviceAddress(device_->device(), &info);
}

void Buffer::destroy() {
    if (buffer_ != VK_NULL_HANDLE)
        vmaDestroyBuffer(device_->allocator(), buffer_, allocation_);
    device_ = nullptr;
    buffer_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    size_ = 0;
    mapped_ = nullptr;
    usage_ = 0;
}

void Buffer::moveFrom(Buffer& other) {
    device_ = other.device_;
    buffer_ = other.buffer_;
    allocation_ = other.allocation_;
    size_ = other.size_;
    mapped_ = other.mapped_;
    usage_ = other.usage_;
    other.device_ = nullptr;
    other.buffer_ = VK_NULL_HANDLE;
    other.allocation_ = VK_NULL_HANDLE;
    other.size_ = 0;
    other.mapped_ = nullptr;
    other.usage_ = 0;
}

} // namespace rhi
