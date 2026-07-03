#pragma once

#include "RHI/VulkanCommon.h"

#include <vector>

namespace rhi {

class Device;

// Growable descriptor-set allocator. Allocates from a current pool and opens
// a bigger one when it runs dry (OUT_OF_POOL_MEMORY / FRAGMENTED_POOL), so no
// pass ever has to predict descriptor counts. Two usage patterns:
//
//   * per-frame: reset() at slot reuse returns every set at once (the
//     Renderer's FrameContexts do this — replaces Phase 1's fixed pools);
//   * persistent: allocate long-lived sets (materials) and never reset.
//
// Pool sizes come from a fixed type-ratio table scaled by setsPerPool; the
// ratios cover the app's descriptor mix and merely need to be roomy, not
// exact, since exhaustion just opens another pool.
class DescriptorAllocator {
public:
    DescriptorAllocator() = default;
    ~DescriptorAllocator() { destroy(); }
    DescriptorAllocator(const DescriptorAllocator&) = delete;
    DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;

    // flags: e.g. VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT for a pool
    // family serving bindless material sets.
    void init(Device& device, uint32_t setsPerPool = 64,
              VkDescriptorPoolCreateFlags flags = 0);
    void destroy();

    // variableCount > 0 allocates the trailing variable-count binding of the
    // layout (descriptorBindingVariableDescriptorCount) with that many
    // descriptors.
    VkDescriptorSet allocate(VkDescriptorSetLayout layout, uint32_t variableCount = 0);

    // Resets every pool this allocator ever opened (all sets die at once).
    void reset();

private:
    VkDescriptorPool createPool(uint32_t sets) const;
    VkDescriptorPool currentPool();

    Device* device_ = nullptr;
    VkDescriptorPoolCreateFlags flags_ = 0;
    uint32_t nextPoolSets_ = 0;
    VkDescriptorPool current_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> usedPools_;
    std::vector<VkDescriptorPool> freePools_;
};

// Batches vkUpdateDescriptorSets writes; keeps the buffer/image info structs
// alive until flush() so callers can write a set in a few readable lines.
class DescriptorWriter {
public:
    DescriptorWriter& writeBuffer(uint32_t binding, VkBuffer buffer,
                                  VkDeviceSize offset, VkDeviceSize range,
                                  VkDescriptorType type);
    DescriptorWriter& writeImage(uint32_t binding, VkImageView view,
                                 VkSampler sampler, VkImageLayout layout,
                                 VkDescriptorType type,
                                 uint32_t arrayElement = 0);

    void flush(VkDevice device, VkDescriptorSet set);

private:
    struct PendingWrite {
        VkWriteDescriptorSet write{};
        size_t infoIndex = 0;
        bool isImage = false;
    };
    std::vector<PendingWrite> writes_;
    std::vector<VkDescriptorBufferInfo> bufferInfos_;
    std::vector<VkDescriptorImageInfo> imageInfos_;
};

} // namespace rhi
