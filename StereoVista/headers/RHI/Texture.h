#pragma once

#include "RHI/VulkanCommon.h"
#include "RHI/Vma.h"

namespace rhi {

class Device;

inline uint32_t computeMipCount(uint32_t width, uint32_t height) {
    uint32_t mips = 1;
    while ((width | height) >> mips)
        ++mips;
    return mips;
}

struct TextureDesc {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    uint32_t mipLevels = 1;   // use computeMipCount() for a full chain
    uint32_t arrayLayers = 1;
    VkImageUsageFlags usage = 0; // TRANSFER_DST/SRC are added automatically
                                 // when upload()/mip generation need them
    // VK_IMAGE_VIEW_TYPE_MAX_ENUM = derive: 1 layer -> 2D, else 2D_ARRAY.
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    // e.g. VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT for cube / cube-array views.
    VkImageCreateFlags createFlags = 0;
    const char* debugName = nullptr;
};

// VMA-backed VkImage plus its whole-image default view. Move-only RAII.
//
// Layout contract: upload() leaves the image SHADER_READ_ONLY_OPTIMAL and is
// the only method that touches layouts. Render-target textures are
// transitioned explicitly by the renderer every frame (UNDEFINED-discard
// pattern); this class deliberately does not track layout state for them.
class Texture {
public:
    Texture() = default;
    ~Texture() { destroy(); }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept { moveFrom(other); }
    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            destroy();
            moveFrom(other);
        }
        return *this;
    }

    void create(Device& device, const TextureDesc& desc);
    void destroy();

    // Staged upload of mip level 0 for ALL layers (tightly packed,
    // layer-major), then generates the remaining mip levels by blit chain
    // when the texture has more than one. Blocks on Device::immediateSubmit —
    // loading-time work, not per-frame. Leaves every subresource in
    // SHADER_READ_ONLY_OPTIMAL.
    void upload(const void* pixels, size_t byteSize);

    // Extra view over a layer range (e.g. one cube face group of a cube array
    // as a 2D_ARRAY attachment view). The CALLER owns the returned view and
    // must vkDestroyImageView it before this texture is destroyed.
    VkImageView createLayerView(VkImageViewType type, uint32_t baseLayer,
                                uint32_t layerCount,
                                const char* debugName = nullptr) const;

    VkImage image() const { return image_; }
    VkImageView view() const { return view_; }
    VkFormat format() const { return desc_.format; }
    VkExtent2D extent() const { return desc_.extent; }
    uint32_t mipLevels() const { return desc_.mipLevels; }
    uint32_t arrayLayers() const { return desc_.arrayLayers; }
    VkImageAspectFlags aspect() const { return aspect_; }
    bool valid() const { return image_ != VK_NULL_HANDLE; }

private:
    void moveFrom(Texture& other);

    Device* device_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    TextureDesc desc_{};
    VkImageAspectFlags aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
};

} // namespace rhi
