#include "RHI/DescriptorAllocator.h"

#include "RHI/Device.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace rhi {

namespace {

// Descriptors per set, by type: generous for the app's mix (camera/material
// UBOs, sampled material textures, point-cloud SSBOs, compute storage
// images). Exhaustion is harmless — the allocator opens another pool.
struct TypeRatio {
    VkDescriptorType type;
    float perSet;
};

const TypeRatio kRatios[] = {
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.0f },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.0f },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.0f },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f },
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1.0f },
    { VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f },
};

constexpr uint32_t kMaxSetsPerPool = 1024;

} // namespace

void DescriptorAllocator::init(Device& device, uint32_t setsPerPool,
                               VkDescriptorPoolCreateFlags flags) {
    destroy();
    device_ = &device;
    flags_ = flags;
    nextPoolSets_ = setsPerPool;
}

VkDescriptorPool DescriptorAllocator::createPool(uint32_t sets) const {
    VkDescriptorPoolSize sizes[std::size(kRatios)];
    for (size_t i = 0; i < std::size(kRatios); ++i) {
        sizes[i].type = kRatios[i].type;
        sizes[i].descriptorCount =
            static_cast<uint32_t>(kRatios[i].perSet * sets) + 1;
    }
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = flags_;
    info.maxSets = sets;
    info.poolSizeCount = static_cast<uint32_t>(std::size(kRatios));
    info.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device_->device(), &info, nullptr, &pool));
    return pool;
}

VkDescriptorPool DescriptorAllocator::currentPool() {
    if (current_ == VK_NULL_HANDLE) {
        if (!freePools_.empty()) {
            current_ = freePools_.back();
            freePools_.pop_back();
        } else {
            current_ = createPool(nextPoolSets_);
            nextPoolSets_ = std::min(nextPoolSets_ * 2, kMaxSetsPerPool);
        }
        usedPools_.push_back(current_);
    }
    return current_;
}

VkDescriptorSet DescriptorAllocator::allocate(VkDescriptorSetLayout layout,
                                              uint32_t variableCount) {
    if (!device_)
        throw std::runtime_error("DescriptorAllocator used before init()");

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable{};
    variable.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variable.descriptorSetCount = 1;
    variable.pDescriptorCounts = &variableCount;

    for (int attempt = 0; attempt < 2; ++attempt) {
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = currentPool();
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &layout;
        if (variableCount > 0)
            alloc.pNext = &variable;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult result = vkAllocateDescriptorSets(device_->device(), &alloc, &set);
        if (result == VK_SUCCESS)
            return set;
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY ||
            result == VK_ERROR_FRAGMENTED_POOL) {
            current_ = VK_NULL_HANDLE; // retire; retry opens a fresh pool
            continue;
        }
        VK_CHECK(result);
    }
    // Two attempts failing means a single set exceeds a brand-new pool —
    // a layout larger than the ratio table allows, not a capacity problem.
    throw std::runtime_error(
        "DescriptorAllocator: set layout exceeds pool ratios (adjust kRatios)");
}

void DescriptorAllocator::reset() {
    if (!device_)
        return;
    for (VkDescriptorPool pool : usedPools_) {
        VK_CHECK(vkResetDescriptorPool(device_->device(), pool, 0));
        freePools_.push_back(pool);
    }
    usedPools_.clear();
    current_ = VK_NULL_HANDLE;
}

void DescriptorAllocator::destroy() {
    if (!device_)
        return;
    for (VkDescriptorPool pool : usedPools_)
        vkDestroyDescriptorPool(device_->device(), pool, nullptr);
    for (VkDescriptorPool pool : freePools_)
        vkDestroyDescriptorPool(device_->device(), pool, nullptr);
    usedPools_.clear();
    freePools_.clear();
    current_ = VK_NULL_HANDLE;
    device_ = nullptr;
}

DescriptorWriter& DescriptorWriter::writeBuffer(uint32_t binding, VkBuffer buffer,
                                                VkDeviceSize offset,
                                                VkDeviceSize range,
                                                VkDescriptorType type) {
    VkDescriptorBufferInfo info{};
    info.buffer = buffer;
    info.offset = offset;
    info.range = range;
    bufferInfos_.push_back(info);

    PendingWrite pending{};
    pending.write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    pending.write.dstBinding = binding;
    pending.write.descriptorCount = 1;
    pending.write.descriptorType = type;
    pending.infoIndex = bufferInfos_.size() - 1;
    pending.isImage = false;
    writes_.push_back(pending);
    return *this;
}

DescriptorWriter& DescriptorWriter::writeImage(uint32_t binding, VkImageView view,
                                               VkSampler sampler,
                                               VkImageLayout layout,
                                               VkDescriptorType type,
                                               uint32_t arrayElement) {
    VkDescriptorImageInfo info{};
    info.sampler = sampler;
    info.imageView = view;
    info.imageLayout = layout;
    imageInfos_.push_back(info);

    PendingWrite pending{};
    pending.write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    pending.write.dstBinding = binding;
    pending.write.dstArrayElement = arrayElement;
    pending.write.descriptorCount = 1;
    pending.write.descriptorType = type;
    pending.infoIndex = imageInfos_.size() - 1;
    pending.isImage = true;
    writes_.push_back(pending);
    return *this;
}

void DescriptorWriter::flush(VkDevice device, VkDescriptorSet set) {
    // Pointers are patched here because the info vectors may have re-allocated
    // while writes were queued.
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(writes_.size());
    for (PendingWrite& pending : writes_) {
        pending.write.dstSet = set;
        if (pending.isImage)
            pending.write.pImageInfo = &imageInfos_[pending.infoIndex];
        else
            pending.write.pBufferInfo = &bufferInfos_[pending.infoIndex];
        writes.push_back(pending.write);
    }
    if (!writes.empty())
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    writes_.clear();
    bufferInfos_.clear();
    imageInfos_.clear();
}

} // namespace rhi
