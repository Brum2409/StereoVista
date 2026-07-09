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
//
// EVICTION (M2, plan §6.5): streaming systems churn textures, so slots are
// reusable. freeTexture(index, retireValue) parks the texture (and its slot)
// in a graveyard keyed on the frame timeline; collectGarbage(completed) —
// called by the Renderer each frame — destroys the VkImage and recycles the
// slot only once the GPU has retired every frame that could still reference
// it (destruction AND descriptor-slot rewrite gate on the same value:
// rewriting a slot an in-flight frame dynamically uses is illegal even under
// update-after-bind). Callers use Renderer::frameRetireValue() as the retire
// value. Material table entries have no GPU lifetime (the table is re-copied
// into a per-frame SSBO), so freeMaterial() recycles immediately.
class MaterialSystem {
public:
    static constexpr uint32_t kTextureCapacity = 4096;

    MaterialSystem() = default;
    ~MaterialSystem() { shutdown(); }
    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;

    void init(rhi::Device& device);
    void shutdown();

    // Takes ownership; returns the texture's bindless index (reuses a
    // recycled slot when one is free).
    uint32_t addTexture(rhi::Texture&& texture);
    // Parks the slot's texture for deferred destruction + slot reuse once the
    // frame timeline passes retireValue. The index must not be referenced by
    // any material used in draws from this frame on.
    void freeTexture(uint32_t index, uint64_t retireValue);
    // True when addTexture can succeed (a free slot exists or capacity left).
    bool textureSlotAvailable() const {
        return !freeSlots_.empty() || textures_.size() < kTextureCapacity;
    }

    // Returns the material's index for DrawItem::materialIndex (reuses freed
    // entries).
    uint32_t addMaterial(const gpu::MaterialData& data);
    // Recycles the entry immediately (safe: per-frame SSBO copies).
    void freeMaterial(uint32_t index);
    gpu::MaterialData& material(uint32_t index) { return materials_[index]; }
    const std::vector<gpu::MaterialData>& materials() const { return materials_; }

    // Destroys graveyard textures whose retire value the frame timeline has
    // passed and recycles their slots. The Renderer calls this once per frame
    // with the completed timeline counter.
    void collectGarbage(uint64_t completedTimelineValue);

    VkDescriptorSetLayout setLayout() const { return layout_; }
    VkDescriptorSet set() const { return set_; }
    uint32_t textureCount() const { return static_cast<uint32_t>(textures_.size()); }
    uint32_t freeTextureSlots() const {
        return static_cast<uint32_t>(freeSlots_.size()) +
               (kTextureCapacity - static_cast<uint32_t>(textures_.size()));
    }
    uint32_t graveyardCount() const { return static_cast<uint32_t>(graveyard_.size()); }

private:
    struct Grave {
        rhi::Texture texture;
        uint32_t slot = 0;
        uint64_t retireValue = 0;
    };

    rhi::Device* device_ = nullptr;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::vector<rhi::Texture> textures_; // freed slots hold an invalid Texture
    std::vector<uint32_t> freeSlots_;
    std::vector<Grave> graveyard_;
    std::vector<gpu::MaterialData> materials_;
    std::vector<uint32_t> freeMaterials_;
};

} // namespace renderer
