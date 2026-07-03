#pragma once

#include "RHI/Texture.h"
#include "RHI/VulkanCommon.h"
#include "Renderer/GpuTypes.h"

#include <vector>

namespace rhi {
class Device;
}

namespace renderer {

// Bindless material registry (playbook A.3 / C.8). Owns:
//   * every loaded material texture and ONE persistent descriptor set holding
//     them all as a partially-bound texture2D array (set 1, binding 1) next
//     to the shared trilinear-aniso sampler (binding 0) — bound once per
//     frame, never rebound per draw;
//   * the CPU material table (gpu::MaterialData). Draws reference materials
//     by index (push constants); materials reference textures by index.
// The whole table is small and re-uploaded into a per-frame SSBO by the
// renderer, so material edits are trivially dynamic.
//
// The array uses UPDATE_AFTER_BIND + PARTIALLY_BOUND with a fixed capacity —
// addTexture() writes the new slot immediately, which is legal mid-frame
// under update-after-bind.
class MaterialSystem {
public:
    static constexpr uint32_t kTextureCapacity = 4096;

    MaterialSystem() = default;
    ~MaterialSystem() { shutdown(); }
    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;

    void init(rhi::Device& device);
    void shutdown();

    // Takes ownership; returns the texture's bindless index.
    uint32_t addTexture(rhi::Texture&& texture);

    // Returns the material's index for DrawItem::materialIndex.
    uint32_t addMaterial(const gpu::MaterialData& data);
    gpu::MaterialData& material(uint32_t index) { return materials_[index]; }
    const std::vector<gpu::MaterialData>& materials() const { return materials_; }

    VkDescriptorSetLayout setLayout() const { return layout_; }
    VkDescriptorSet set() const { return set_; }
    uint32_t textureCount() const { return static_cast<uint32_t>(textures_.size()); }

private:
    rhi::Device* device_ = nullptr;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::vector<rhi::Texture> textures_;
    std::vector<gpu::MaterialData> materials_;
};

} // namespace renderer
