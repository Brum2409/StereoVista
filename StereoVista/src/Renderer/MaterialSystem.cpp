#include "Renderer/MaterialSystem.h"

#include "RHI/Device.h"

#include <algorithm>
#include <stdexcept>

namespace renderer {

void MaterialSystem::init(rhi::Device& device) {
    shutdown();
    device_ = &device;

    // Shared material sampler: trilinear + anisotropy + repeat, matching the
    // GL loader's per-texture state.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy =
        std::min(16.0f, device.properties().limits.maxSamplerAnisotropy);
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    VK_CHECK(vkCreateSampler(device.device(), &samplerInfo, nullptr, &sampler_));

    // Set layout: binding 0 = sampler, binding 1 = the bindless texture
    // array. Hand-built (not reflected) because it is deliberately SHARED
    // across pipelines via externalSetLayout — this is the one set layout the
    // shaders don't own.
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[1].descriptorCount = kTextureCapacity;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    const VkDescriptorBindingFlags arrayFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    VkDescriptorBindingFlags bindingFlags[2] = { 0, arrayFlags };
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount = 2;
    flagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(device.device(), &layoutInfo, nullptr, &layout_));

    // Exact-size dedicated pool for the single persistent set.
    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    sizes[0].descriptorCount = 1;
    sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    sizes[1].descriptorCount = kTextureCapacity;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &pool_));

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout_;
    VK_CHECK(vkAllocateDescriptorSets(device.device(), &alloc, &set_));

    VkDescriptorImageInfo samplerWriteInfo{};
    samplerWriteInfo.sampler = sampler_;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = &samplerWriteInfo;
    vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
}

uint32_t MaterialSystem::addTexture(rhi::Texture&& texture) {
    if (!device_)
        throw std::runtime_error("MaterialSystem used before init()");

    uint32_t index = 0;
    if (!freeSlots_.empty()) {
        index = freeSlots_.back();
        freeSlots_.pop_back();
        textures_[index] = std::move(texture);
    } else {
        if (textures_.size() >= kTextureCapacity)
            throw std::runtime_error(
                "MaterialSystem: bindless texture capacity exceeded");
        index = static_cast<uint32_t>(textures_.size());
        textures_.push_back(std::move(texture));
    }

    VkDescriptorImageInfo info{};
    info.imageView = textures_[index].view();
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 1;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(device_->device(), 1, &write, 0, nullptr);
    return index;
}

void MaterialSystem::freeTexture(uint32_t index, uint64_t retireValue) {
    if (index >= textures_.size() || !textures_[index].valid())
        return; // already freed / never lived — idempotent
    Grave grave;
    grave.texture = std::move(textures_[index]);
    grave.slot = index;
    grave.retireValue = retireValue;
    graveyard_.push_back(std::move(grave));
    // The slot joins freeSlots_ in collectGarbage(), together with the
    // destroy — reuse rewrites the descriptor, which is only legal once no
    // in-flight frame can still sample the old image through it.
}

void MaterialSystem::collectGarbage(uint64_t completedTimelineValue) {
    size_t kept = 0;
    for (size_t i = 0; i < graveyard_.size(); ++i) {
        if (graveyard_[i].retireValue <= completedTimelineValue) {
            freeSlots_.push_back(graveyard_[i].slot);
            graveyard_[i].texture.destroy();
        } else {
            if (kept != i)
                graveyard_[kept] = std::move(graveyard_[i]);
            ++kept;
        }
    }
    graveyard_.resize(kept);
}

uint32_t MaterialSystem::addMaterial(const gpu::MaterialData& data) {
    if (!freeMaterials_.empty()) {
        const uint32_t index = freeMaterials_.back();
        freeMaterials_.pop_back();
        materials_[index] = data;
        return index;
    }
    materials_.push_back(data);
    return static_cast<uint32_t>(materials_.size() - 1);
}

void MaterialSystem::freeMaterial(uint32_t index) {
    if (index >= materials_.size())
        return;
    // Scrub so a stale reference can't sample a recycled texture slot.
    gpu::MaterialData scrubbed{};
    scrubbed.baseColor = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f); // debug magenta
    scrubbed.albedoTexture = kInvalidTexture;
    scrubbed.normalTexture = kInvalidTexture;
    scrubbed.metallicTexture = kInvalidTexture;
    scrubbed.roughnessTexture = kInvalidTexture;
    scrubbed.aoTexture = kInvalidTexture;
    materials_[index] = scrubbed;
    freeMaterials_.push_back(index);
}

void MaterialSystem::shutdown() {
    if (!device_)
        return;
    textures_.clear();
    freeSlots_.clear();
    graveyard_.clear();
    materials_.clear();
    freeMaterials_.clear();
    if (pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device_->device(), pool_, nullptr);
    if (layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device_->device(), layout_, nullptr);
    if (sampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_->device(), sampler_, nullptr);
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    set_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    device_ = nullptr;
}

} // namespace renderer
