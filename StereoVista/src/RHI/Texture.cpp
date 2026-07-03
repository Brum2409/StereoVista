#include "RHI/Texture.h"

#include "RHI/Barrier.h"
#include "RHI/Buffer.h"
#include "RHI/Device.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace rhi {

namespace {

bool isDepthFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

bool hasStencil(VkFormat format) {
    switch (format) {
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

// Bytes per texel for the uncompressed formats upload() accepts. Compressed
// formats need a different copy layout — add them when an asset path does.
uint32_t texelSize(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8_UINT:
        return 1;
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_R16_UNORM:
        return 2;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R32_UINT:
        return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        throw std::runtime_error("rhi::Texture: no texel size for format " +
                                 std::to_string(static_cast<int>(format)) +
                                 " (unsupported upload format)");
    }
}

} // namespace

void Texture::create(Device& device, const TextureDesc& desc) {
    destroy();
    if (desc.format == VK_FORMAT_UNDEFINED || desc.extent.width == 0 ||
        desc.extent.height == 0)
        throw std::runtime_error("rhi::Texture: invalid TextureDesc");

    device_ = &device;
    desc_ = desc;
    aspect_ = isDepthFormat(desc.format) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                         : VK_IMAGE_ASPECT_COLOR_BIT;
    if (hasStencil(desc.format))
        aspect_ |= VK_IMAGE_ASPECT_STENCIL_BIT;

    VkImageUsageFlags usage = desc.usage;
    if (desc.mipLevels > 1)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.flags = desc.createFlags;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = desc.format;
    imgInfo.extent = { desc.extent.width, desc.extent.height, 1 };
    imgInfo.mipLevels = desc.mipLevels;
    imgInfo.arrayLayers = desc.arrayLayers;
    imgInfo.samples = desc.samples;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = usage;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    // Attachments and other large targets get their own allocation; small
    // sampled textures suballocate.
    if (usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VK_CHECK(vmaCreateImage(device_->allocator(), &imgInfo, &allocInfo, &image_,
                            &allocation_, nullptr));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = desc.viewType != VK_IMAGE_VIEW_TYPE_MAX_ENUM
                            ? desc.viewType
                            : (desc.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                    : VK_IMAGE_VIEW_TYPE_2D);
    viewInfo.format = desc.format;
    viewInfo.subresourceRange.aspectMask = aspect_;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.layerCount = desc.arrayLayers;
    VK_CHECK(vkCreateImageView(device_->device(), &viewInfo, nullptr, &view_));

    if (desc.debugName) {
        device_->setDebugName(VK_OBJECT_TYPE_IMAGE,
                              reinterpret_cast<uint64_t>(image_), desc.debugName);
        device_->setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW,
                              reinterpret_cast<uint64_t>(view_), desc.debugName);
    }
}

VkImageView Texture::createLayerView(VkImageViewType type, uint32_t baseLayer,
                                     uint32_t layerCount,
                                     const char* debugName) const {
    if (!valid())
        throw std::runtime_error("rhi::Texture::createLayerView on an invalid texture");
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = type;
    viewInfo.format = desc_.format;
    viewInfo.subresourceRange.aspectMask = aspect_;
    viewInfo.subresourceRange.levelCount = desc_.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = baseLayer;
    viewInfo.subresourceRange.layerCount = layerCount;
    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(device_->device(), &viewInfo, nullptr, &view));
    if (debugName)
        device_->setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW,
                              reinterpret_cast<uint64_t>(view), debugName);
    return view;
}

void Texture::upload(const void* pixels, size_t byteSize) {
    if (!valid())
        throw std::runtime_error("rhi::Texture::upload on an invalid texture");
    if (aspect_ != VK_IMAGE_ASPECT_COLOR_BIT)
        throw std::runtime_error("rhi::Texture::upload supports color formats only");

    const VkDeviceSize layerBytes = VkDeviceSize(desc_.extent.width) *
                                    desc_.extent.height * texelSize(desc_.format);
    const VkDeviceSize expected = layerBytes * desc_.arrayLayers;
    if (byteSize != expected)
        throw std::runtime_error("rhi::Texture::upload: byte size mismatch (got " +
                                 std::to_string(byteSize) + ", expected " +
                                 std::to_string(expected) + ")");

    Buffer staging;
    BufferDesc stagingDesc{};
    stagingDesc.size = expected;
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.memory = MemoryUsage::HostUpload;
    staging.create(*device_, stagingDesc);
    std::memcpy(staging.mapped(), pixels, byteSize);
    staging.flush();

    const uint32_t mips = desc_.mipLevels;
    const uint32_t layers = desc_.arrayLayers;

    // Mip generation blits linearly; verify the format supports it up front
    // (universal for the 8/16-bit color formats on NVIDIA/AMD, but fail loud
    // rather than silently producing garbage on an exotic one).
    if (mips > 1) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(device_->physicalDevice(), desc_.format,
                                            &props);
        const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                            VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((props.optimalTilingFeatures & needed) != needed)
            throw std::runtime_error(
                "rhi::Texture::upload: format cannot generate mips via linear blit");
    }

    device_->immediateSubmit([&](VkCommandBuffer cmd) {
        // Whole image -> TRANSFER_DST for the base-level copy.
        cmdImageBarrier(cmd, imageBarrier(image_, aspect_,
                                          VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                                          VK_PIPELINE_STAGE_2_COPY_BIT,
                                          VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                          VK_IMAGE_LAYOUT_UNDEFINED,
                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));

        VkBufferImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        region.imageSubresource.aspectMask = aspect_;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = layers;
        region.imageExtent = { desc_.extent.width, desc_.extent.height, 1 };
        VkCopyBufferToImageInfo2 copy{};
        copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
        copy.srcBuffer = staging.handle();
        copy.dstImage = image_;
        copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copy.regionCount = 1;
        copy.pRegions = &region;
        vkCmdCopyBufferToImage2(cmd, &copy);

        // Blit chain: level i-1 (TRANSFER_SRC) -> level i (TRANSFER_DST),
        // all layers per blit.
        int32_t srcW = static_cast<int32_t>(desc_.extent.width);
        int32_t srcH = static_cast<int32_t>(desc_.extent.height);
        for (uint32_t level = 1; level < mips; ++level) {
            cmdImageBarrier(cmd, imageBarrier(image_, aspect_,
                                              VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                                              VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                              VK_PIPELINE_STAGE_2_BLIT_BIT,
                                              VK_ACCESS_2_TRANSFER_READ_BIT,
                                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                              level - 1, 1));

            const int32_t dstW = std::max(srcW / 2, 1);
            const int32_t dstH = std::max(srcH / 2, 1);
            VkImageBlit2 blitRegion{};
            blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            blitRegion.srcSubresource.aspectMask = aspect_;
            blitRegion.srcSubresource.mipLevel = level - 1;
            blitRegion.srcSubresource.layerCount = layers;
            blitRegion.srcOffsets[1] = { srcW, srcH, 1 };
            blitRegion.dstSubresource.aspectMask = aspect_;
            blitRegion.dstSubresource.mipLevel = level;
            blitRegion.dstSubresource.layerCount = layers;
            blitRegion.dstOffsets[1] = { dstW, dstH, 1 };
            VkBlitImageInfo2 blit{};
            blit.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
            blit.srcImage = image_;
            blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blit.dstImage = image_;
            blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blit.regionCount = 1;
            blit.pRegions = &blitRegion;
            blit.filter = VK_FILTER_LINEAR;
            vkCmdBlitImage2(cmd, &blit);

            srcW = dstW;
            srcH = dstH;
        }

        // Everything -> SHADER_READ_ONLY. Levels 0..mips-2 are TRANSFER_SRC,
        // the last level is TRANSFER_DST (or level 0 when there is no chain).
        if (mips > 1)
            cmdImageBarrier(cmd, imageBarrier(image_, aspect_,
                                              VK_PIPELINE_STAGE_2_BLIT_BIT,
                                              VK_ACCESS_2_TRANSFER_READ_BIT,
                                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                              0, mips - 1));
        cmdImageBarrier(cmd, imageBarrier(image_, aspect_,
                                          VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                                          VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                          mips - 1, 1));
    });
}

void Texture::destroy() {
    if (device_) {
        if (view_ != VK_NULL_HANDLE)
            vkDestroyImageView(device_->device(), view_, nullptr);
        if (image_ != VK_NULL_HANDLE)
            vmaDestroyImage(device_->allocator(), image_, allocation_);
    }
    device_ = nullptr;
    image_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    desc_ = TextureDesc{};
    aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
}

void Texture::moveFrom(Texture& other) {
    device_ = other.device_;
    image_ = other.image_;
    allocation_ = other.allocation_;
    view_ = other.view_;
    desc_ = other.desc_;
    aspect_ = other.aspect_;
    other.device_ = nullptr;
    other.image_ = VK_NULL_HANDLE;
    other.allocation_ = VK_NULL_HANDLE;
    other.view_ = VK_NULL_HANDLE;
    other.desc_ = TextureDesc{};
    other.aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
}

} // namespace rhi
