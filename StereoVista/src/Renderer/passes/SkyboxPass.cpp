#include "Renderer/passes/SkyboxPass.h"

#include "Platform/Paths.h"
#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"

#include <glm/gtc/packing.hpp>

#include <stb_image.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace renderer {

namespace {

struct SkyPush {
    glm::vec4 colorA;
    glm::vec4 colorB;
    int32_t mode;
    float intensity;
};

rhi::Texture createDummy(rhi::Device& device, bool cube) {
    const uint8_t black[4 * 6] = {}; // enough bytes for 6 layers of 1x1 RGBA
    rhi::TextureDesc desc{};
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    desc.extent = { 1, 1 };
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (cube) {
        desc.arrayLayers = 6;
        desc.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        desc.createFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    desc.debugName = cube ? "sky dummy cube" : "sky dummy 2D";
    rhi::Texture texture;
    texture.create(device, desc);
    texture.upload(black, cube ? sizeof(black) : 4);
    return texture;
}

} // namespace

void SkyboxPass::init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
                      VkFormat colorFormat, VkFormat depthFormat, uint32_t viewMask,
                      VkDescriptorSetLayout frameSetLayout) {
    shutdown();
    device_ = &device;

    pipeline_ =
        rhi::GraphicsPipelineBuilder{}
            .setShaders(shaderCompiler.load("assets/shaders_vk/skybox.vert"),
                        shaderCompiler.load("assets/shaders_vk/skybox.frag"))
            .setColorFormats({ colorFormat })
            .setDepthFormat(depthFormat)
            .setViewMask(viewMask)
            .setCullMode(VK_CULL_MODE_NONE)
            // Test against reverse-Z far (cleared 0), pass only where nothing
            // was drawn; never write.
            .setDepth(true, false, VK_COMPARE_OP_GREATER_OR_EQUAL)
            .externalSetLayout(0, frameSetLayout)
            .setDebugName("skybox")
            .build(device);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    VK_CHECK(vkCreateSampler(device.device(), &samplerInfo, nullptr, &sampler_));

    dummyCube_ = createDummy(device, true);
    dummy2D_ = createDummy(device, false);
}

bool SkyboxPass::loadEquirectHdr(const std::string& path) {
    const std::filesystem::path resolved = Platform::resolveAssetPath(path);
    if (resolved.empty()) {
        std::cerr << "[sky] HDR not found: " << path << "\n";
        return false;
    }

    int width = 0, height = 0, comp = 0;
    stbi_set_flip_vertically_on_load(false); // row 0 = top = +Y, matches the
                                             // acos(dir.y) mapping in skybox.frag
    float* pixels = stbi_loadf(resolved.string().c_str(), &width, &height, &comp, 4);
    if (!pixels) {
        std::cerr << "[sky] failed to decode " << resolved.string() << ": "
                  << stbi_failure_reason() << "\n";
        return false;
    }

    // float RGBA -> half RGBA (RGBA16F): half the memory, plenty of range.
    const size_t texels = size_t(width) * height;
    std::vector<uint32_t> half(texels * 2);
    for (size_t i = 0; i < texels; ++i) {
        const float* px = pixels + i * 4;
        half[i * 2 + 0] = glm::packHalf2x16(glm::vec2(px[0], px[1]));
        half[i * 2 + 1] = glm::packHalf2x16(glm::vec2(px[2], px[3]));
    }
    stbi_image_free(pixels);

    rhi::TextureDesc desc{};
    desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    desc.extent = { uint32_t(width), uint32_t(height) };
    desc.mipLevels = rhi::computeMipCount(uint32_t(width), uint32_t(height));
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.debugName = "sky equirect HDR";
    rhi::Texture texture;
    texture.create(*device_, desc);
    texture.upload(half.data(), half.size() * sizeof(uint32_t));

    equirect_ = std::move(texture);
    equirectLoaded_ = true;
    std::cout << "[sky] loaded HDR " << resolved.string() << " (" << width << "x"
              << height << ")\n";
    return true;
}

bool SkyboxPass::loadCubemap(const std::string& directory) {
    // Cube layer order +X,-X,+Y,-Y,+Z,-Z = the classic face file names.
    static const char* kFaces[6] = { "right", "left", "top", "bottom", "front", "back" };
    static const char* kExtensions[] = { ".jpg", ".png", ".jpeg", ".bmp", ".tga" };

    stbi_set_flip_vertically_on_load(false);
    std::vector<uint8_t> pixels;
    int faceWidth = 0, faceHeight = 0;
    for (int face = 0; face < 6; ++face) {
        std::filesystem::path facePath;
        for (const char* ext : kExtensions) {
            facePath = Platform::resolveAssetPath(directory + "/" + kFaces[face] + ext);
            if (!facePath.empty())
                break;
        }
        if (facePath.empty()) {
            std::cerr << "[sky] cubemap face '" << kFaces[face] << "' not found in "
                      << directory << "\n";
            return false;
        }
        int width = 0, height = 0, comp = 0;
        uint8_t* data = stbi_load(facePath.string().c_str(), &width, &height, &comp, 4);
        if (!data) {
            std::cerr << "[sky] failed to decode " << facePath.string() << ": "
                      << stbi_failure_reason() << "\n";
            return false;
        }
        if (face == 0) {
            faceWidth = width;
            faceHeight = height;
            pixels.resize(size_t(width) * height * 4 * 6);
        } else if (width != faceWidth || height != faceHeight) {
            std::cerr << "[sky] cubemap face size mismatch in " << directory << "\n";
            stbi_image_free(data);
            return false;
        }
        std::memcpy(pixels.data() + size_t(face) * faceWidth * faceHeight * 4, data,
                    size_t(faceWidth) * faceHeight * 4);
        stbi_image_free(data);
    }

    rhi::TextureDesc desc{};
    desc.format = VK_FORMAT_R8G8B8A8_SRGB; // LDR faces: hardware sRGB decode
    desc.extent = { uint32_t(faceWidth), uint32_t(faceHeight) };
    desc.mipLevels = rhi::computeMipCount(uint32_t(faceWidth), uint32_t(faceHeight));
    desc.arrayLayers = 6;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    desc.createFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    desc.debugName = "sky cubemap";
    rhi::Texture texture;
    texture.create(*device_, desc);
    texture.upload(pixels.data(), pixels.size());

    cubemap_ = std::move(texture);
    cubemapLoaded_ = true;
    std::cout << "[sky] loaded cubemap " << directory << " (" << faceWidth << "x"
              << faceHeight << " per face)\n";
    return true;
}

void SkyboxPass::record(VkCommandBuffer cmd, VkDescriptorSet frameSet,
                        const SkyState& sky) const {
    SkyPush push{};
    push.mode = static_cast<int32_t>(sky.mode);
    push.intensity = sky.intensity;
    switch (sky.mode) {
    case SkyMode::SolidColor:
        push.colorA = glm::vec4(sky.solidColor, 1.0f);
        break;
    case SkyMode::Gradient:
        push.colorA = glm::vec4(sky.gradientBottom, 1.0f);
        push.colorB = glm::vec4(sky.gradientTop, 1.0f);
        break;
    default:
        break;
    }
    // Degrade unloaded texture modes to the solid color instead of sampling
    // a black dummy.
    if ((sky.mode == SkyMode::Cubemap && !cubemapLoaded_) ||
        (sky.mode == SkyMode::Equirect && !equirectLoaded_)) {
        push.mode = static_cast<int32_t>(SkyMode::SolidColor);
        push.colorA = glm::vec4(sky.solidColor, 1.0f);
    }

    pipeline_.bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.layout(),
                            0, 1, &frameSet, 0, nullptr);
    pipeline_.pushConstants(cmd, &push, sizeof(push));
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void SkyboxPass::shutdown() {
    if (!device_)
        return;
    pipeline_.destroy();
    if (sampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_->device(), sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    cubemap_.destroy();
    equirect_.destroy();
    dummyCube_.destroy();
    dummy2D_.destroy();
    cubemapLoaded_ = false;
    equirectLoaded_ = false;
    device_ = nullptr;
}

} // namespace renderer
