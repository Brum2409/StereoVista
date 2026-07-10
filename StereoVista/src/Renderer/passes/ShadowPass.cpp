#include "Renderer/passes/ShadowPass.h"

#include "RHI/Barrier.h"
#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"
#include "Renderer/MeshBuffer.h"
#include "Renderer/Projection.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace renderer {

namespace {

// Comparison sampler: reverse-Z "lit" test (reference >= stored nearest
// caster depth) with hardware 2x2 PCF via linear filtering.
VkSampler createCompareSampler(rhi::Device& device, VkSamplerAddressMode address) {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = address;
    info.addressModeV = address;
    info.addressModeW = address;
    // Border depth 0 = reverse-Z far: outside the map compares as lit.
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    info.compareEnable = VK_TRUE;
    info.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device.device(), &info, nullptr, &sampler));
    return sampler;
}

// How a draw participates in the shadow passes, from its material.
enum class CasterKind { Skip, Opaque, Masked };

CasterKind classifyCaster(const DrawItem& draw,
                          const std::vector<gpu::MaterialData>& materials) {
    if (!draw.castsShadows || !draw.mesh)
        return CasterKind::Skip;
    if (draw.materialIndex >= materials.size())
        return CasterKind::Opaque; // defensive: unknown material casts solid
    const gpu::MaterialData& mat = materials[draw.materialIndex];
    if (!(mat.flags & SV_MATERIAL_ALPHA_MASK))
        return CasterKind::Opaque;
    if (mat.albedoTexture != kInvalidTexture)
        return CasterKind::Masked;
    // Flat-color mask: the whole surface passes or fails the cutoff at once
    // (mesh.frag applies the same test), so it casts solid or not at all —
    // no per-fragment work needed.
    return mat.baseColor.a < mat.alphaCutoff ? CasterKind::Skip
                                             : CasterKind::Opaque;
}

} // namespace

void ShadowPass::init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
                      VkDescriptorSetLayout frameSetLayout,
                      VkDescriptorSetLayout materialSetLayout) {
    shutdown();
    device_ = &device;

    rhi::TextureDesc sunDesc{};
    sunDesc.format = VK_FORMAT_D32_SFLOAT;
    sunDesc.extent = { kSunShadowResolution, kSunShadowResolution };
    sunDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    sunDesc.debugName = "sun shadow map";
    sunShadow_.create(device, sunDesc);

    rhi::TextureDesc pointDesc{};
    pointDesc.format = VK_FORMAT_D32_SFLOAT;
    pointDesc.extent = { kPointShadowResolution, kPointShadowResolution };
    pointDesc.arrayLayers = 6 * kMaxShadowedPointLights;
    pointDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    pointDesc.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    pointDesc.createFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    pointDesc.debugName = "point shadow cube array";
    pointShadow_.create(device, pointDesc);

    for (uint32_t i = 0; i < kMaxShadowedPointLights; ++i)
        pointShadowFaceViews_[i] = pointShadow_.createLayerView(
            VK_IMAGE_VIEW_TYPE_2D_ARRAY, i * 6, 6, "point shadow faces");

    compareSampler2D_ =
        createCompareSampler(device, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    compareSamplerCube_ =
        createCompareSampler(device, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    // Raw depth reads for the PCSS blocker search. NEAREST because linear
    // filtering of D32 is an optional format feature (and we average blocker
    // depths ourselves anyway); border depth 0 = reverse-Z far = "no blocker"
    // outside the sun map.
    {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        VK_CHECK(vkCreateSampler(device.device(), &info, nullptr, &rawSampler_));
    }

    // Depth-only casters, cull NONE (non-watertight meshes must still write
    // depth from both sides — GL parity), small NEGATIVE reverse-Z slope bias.
    sunPipeline_ =
        rhi::GraphicsPipelineBuilder{}
            .setShaders(shaderCompiler.load("assets/shaders_vk/depth_sun.vert"))
            .setDepthFormat(VK_FORMAT_D32_SFLOAT)
            .setDepth(true, true)
            .setCullMode(VK_CULL_MODE_NONE)
            .setDepthBias(-1.0f, -2.0f)
            .addVertexBinding(MeshBuffer::positionOnlyBinding())
            .externalSetLayout(0, frameSetLayout)
            .setDebugName("sun shadow casters")
            .build(device);

    pointPipeline_ =
        rhi::GraphicsPipelineBuilder{}
            .setShaders(shaderCompiler.load("assets/shaders_vk/depth_point.vert"))
            .setDepthFormat(VK_FORMAT_D32_SFLOAT)
            .setViewMask(0x3F) // 6 faces in one pass
            .setDepth(true, true)
            .setCullMode(VK_CULL_MODE_NONE)
            .setDepthBias(-1.0f, -2.0f)
            .addVertexBinding(MeshBuffer::positionOnlyBinding())
            .externalSetLayout(0, frameSetLayout)
            .setDebugName("point shadow casters")
            .build(device);

    // Alpha-masked twins: a fragment stage samples the material's albedo
    // alpha (set 1 = the bindless array; the material table already rides
    // the frame set at binding 2) and discards below alphaCutoff, so cutout
    // geometry shadows its silhouette. Only masked casters pay the fragment
    // cost — opaque ones keep the fragment-less early-Z pipelines above.
    sunMaskedPipeline_ =
        rhi::GraphicsPipelineBuilder{}
            .setShaders(shaderCompiler.load("assets/shaders_vk/depth_sun_masked.vert"),
                        shaderCompiler.load("assets/shaders_vk/depth_masked.frag"))
            .setDepthFormat(VK_FORMAT_D32_SFLOAT)
            .setDepth(true, true)
            .setCullMode(VK_CULL_MODE_NONE)
            .setDepthBias(-1.0f, -2.0f)
            .addVertexBinding(MeshBuffer::positionUvBinding())
            .externalSetLayout(0, frameSetLayout)
            .externalSetLayout(1, materialSetLayout)
            .setDebugName("sun shadow casters (masked)")
            .build(device);

    pointMaskedPipeline_ =
        rhi::GraphicsPipelineBuilder{}
            .setShaders(shaderCompiler.load("assets/shaders_vk/depth_point_masked.vert"),
                        shaderCompiler.load("assets/shaders_vk/depth_masked.frag"))
            .setDepthFormat(VK_FORMAT_D32_SFLOAT)
            .setViewMask(0x3F)
            .setDepth(true, true)
            .setCullMode(VK_CULL_MODE_NONE)
            .setDepthBias(-1.0f, -2.0f)
            .addVertexBinding(MeshBuffer::positionUvBinding())
            .externalSetLayout(0, frameSetLayout)
            .externalSetLayout(1, materialSetLayout)
            .setDebugName("point shadow casters (masked)")
            .build(device);
}

std::vector<int> ShadowPass::prepare(const FrameSubmission& submission,
                                     gpu::FrameData& frame) const {
    // ---- Sun ortho fit (ported from GL calculateLightSpaceMatrix) ----
    glm::vec3 sceneMin(FLT_MAX), sceneMax(-FLT_MAX);
    for (const DrawItem& draw : submission.draws) {
        sceneMin = glm::min(sceneMin, draw.worldBoundsCenter - glm::vec3(draw.worldBoundsRadius));
        sceneMax = glm::max(sceneMax, draw.worldBoundsCenter + glm::vec3(draw.worldBoundsRadius));
    }
    if (submission.draws.empty()) {
        sceneMin = submission.views[0].position - glm::vec3(10.0f);
        sceneMax = submission.views[0].position + glm::vec3(10.0f);
    }

    const glm::vec3 sceneCenter = (sceneMin + sceneMax) * 0.5f;
    float sceneRadius = glm::length(sceneMax - sceneMin) * 0.5f;
    sceneRadius = std::max(sceneRadius, 1.0f);

    const glm::vec3 lightDir =
        glm::dot(submission.sun.direction, submission.sun.direction) > 1e-12f
            ? glm::normalize(submission.sun.direction)
            : glm::vec3(0.0f, -1.0f, 0.0f); // matches buildFrameData's guard
    const float lightDistance = sceneRadius * 2.0f;
    const glm::vec3 lightPos = sceneCenter - lightDir * lightDistance;

    const float orthoSize = sceneRadius * 1.03f; // tight fit = texel density
    const float nearPlane = std::max(lightDistance - sceneRadius * 1.5f, 0.05f);
    const float farPlane = lightDistance + sceneRadius * 1.5f;

    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(lightDir, up)) > 0.99f)
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, up);
    glm::mat4 lightProj =
        renderer::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, nearPlane, farPlane);

    // Texel snapping: pin the projected world origin to whole texels so the
    // shadow pattern doesn't crawl as the fitted frustum drifts per frame.
    {
        const glm::mat4 lightSpace = lightProj * lightView;
        glm::vec4 shadowOrigin = lightSpace * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        shadowOrigin *= float(kSunShadowResolution) * 0.5f;
        glm::vec4 roundOffset = glm::round(shadowOrigin) - shadowOrigin;
        roundOffset *= 2.0f / float(kSunShadowResolution);
        roundOffset.z = 0.0f;
        roundOffset.w = 0.0f;
        lightProj[3] += roundOffset;
    }

    frame.sunViewProj = lightProj * lightView;
    frame.shadowTexelWorldSize = (2.0f * orthoSize) / float(kSunShadowResolution);
    // Lets the receiver express its depth bias in WORLD units: the ortho
    // depth axis is linear, so one world unit toward the sun is exactly this
    // much ndc.z. (A constant NDC bias would grow with the fitted scene size.)
    frame.sunWorldToDepth = 1.0f / (farPlane - nearPlane);

    // ---- Point light cube faces + slot assignment ----
    // Far plane from the submission (panel-editable; the GL app hardcoded
    // far_plane = 50). record()'s range cull uses the same value. Depth
    // precision spreads over the range, so the clamp only guards degeneracy.
    const float pointFar =
        std::max(submission.pointShadowRange, kPointShadowNear + 0.5f);
    pointShadowFar_ = pointFar;
    frame.pointShadowNear = kPointShadowNear;
    frame.pointShadowFar = pointFar;
    const glm::mat4 faceProj = perspectiveCubeFace(kPointShadowNear, pointFar);

    std::vector<int> slots;
    slots.reserve(submission.pointLights.size());
    uint32_t nextSlot = 0;
    const size_t lightCount =
        std::min<size_t>(submission.pointLights.size(), kMaxPointLights);
    for (size_t i = 0; i < submission.pointLights.size(); ++i) {
        const PointLightState& light = submission.pointLights[i];
        int slot = -1;
        if (i < lightCount && light.castsShadows && submission.shadowsEnabled &&
            nextSlot < kMaxShadowedPointLights) {
            slot = static_cast<int>(nextSlot++);
            slotLightPos_[slot] = light.position;
            for (int face = 0; face < 6; ++face)
                frame.pointShadowFaceVP[slot * 6 + face] =
                    faceProj * cubeFaceView(light.position, face);
        }
        slots.push_back(slot);
    }
    return slots;
}

void ShadowPass::record(VkCommandBuffer cmd, VkDescriptorSet frameSet,
                        VkDescriptorSet materialSet,
                        const std::vector<gpu::MaterialData>& materials,
                        const FrameSubmission& submission,
                        uint32_t shadowedLightCount) {
    // Fresh maps every frame: previous contents are irrelevant.
    {
        VkImageMemoryBarrier2 barriers[2] = {
            rhi::imageBarrier(sunShadow_.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL),
            rhi::imageBarrier(pointShadow_.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL),
        };
        rhi::cmdBarrier(cmd, barriers, 2);
    }

    const bool castersEnabled = submission.shadowsEnabled;

    // ---- Sun shadow map ----
    {
        VkRenderingAttachmentInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = sunShadow_.view();
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil.depth = 0.0f; // reverse-Z far

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = { kSunShadowResolution, kSunShadowResolution };
        rendering.layerCount = 1;
        rendering.pDepthAttachment = &depth;
        vkCmdBeginRendering(cmd, &rendering);

        if (castersEnabled && submission.sun.enabled && !submission.draws.empty()) {
            VkViewport viewport{};
            viewport.width = float(kSunShadowResolution);
            viewport.height = float(kSunShadowResolution);
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{};
            scissor.extent = rendering.renderArea.extent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // Opaque casters first (fragment-less early-Z pipeline), then the
            // alpha-masked set under one pipeline + material-set bind.
            bool boundOpaque = false;
            bool anyMasked = false;
            for (const DrawItem& draw : submission.draws) {
                const CasterKind kind = classifyCaster(draw, materials);
                if (kind == CasterKind::Masked)
                    anyMasked = true;
                if (kind != CasterKind::Opaque)
                    continue;
                if (!boundOpaque) {
                    sunPipeline_.bind(cmd);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            sunPipeline_.layout(), 0, 1, &frameSet,
                                            0, nullptr);
                    boundOpaque = true;
                }
                gpu::DepthSunPush push{};
                push.model = draw.model;
                sunPipeline_.pushConstants(cmd, &push, sizeof(push));
                draw.mesh->bindAndDraw(cmd);
            }
            if (anyMasked) {
                sunMaskedPipeline_.bind(cmd);
                VkDescriptorSet sets[2] = { frameSet, materialSet };
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        sunMaskedPipeline_.layout(), 0, 2, sets,
                                        0, nullptr);
                for (const DrawItem& draw : submission.draws) {
                    if (classifyCaster(draw, materials) != CasterKind::Masked)
                        continue;
                    gpu::DepthMaskedPush push{};
                    push.model = draw.model;
                    push.materialIndex = draw.materialIndex;
                    sunMaskedPipeline_.pushConstants(cmd, &push, sizeof(push));
                    draw.mesh->bindAndDraw(cmd);
                }
            }
        }
        vkCmdEndRendering(cmd);
    }

    // ---- Point light cubemaps (one multiview pass per shadowed light) ----
    for (uint32_t slot = 0; slot < shadowedLightCount; ++slot) {
        VkRenderingAttachmentInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = pointShadowFaceViews_[slot];
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil.depth = 0.0f;

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = { kPointShadowResolution, kPointShadowResolution };
        rendering.layerCount = 1;
        rendering.viewMask = 0x3F;
        rendering.pDepthAttachment = &depth;
        vkCmdBeginRendering(cmd, &rendering);

        if (castersEnabled && !submission.draws.empty()) {
            VkViewport viewport{};
            viewport.width = float(kPointShadowResolution);
            viewport.height = float(kPointShadowResolution);
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{};
            scissor.extent = rendering.renderArea.extent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // Range cull: a caster entirely beyond the shadow far plane
            // cannot darken anything this light reaches. With many meshes
            // only the light's neighborhood pays vertex cost.
            auto inRange = [&](const DrawItem& draw) {
                return glm::distance(draw.worldBoundsCenter, slotLightPos_[slot]) <=
                       pointShadowFar_ + draw.worldBoundsRadius;
            };

            bool boundOpaque = false;
            bool anyMasked = false;
            for (const DrawItem& draw : submission.draws) {
                const CasterKind kind = classifyCaster(draw, materials);
                if (kind == CasterKind::Skip || !inRange(draw))
                    continue;
                if (kind == CasterKind::Masked) {
                    anyMasked = true;
                    continue;
                }
                if (!boundOpaque) {
                    pointPipeline_.bind(cmd);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pointPipeline_.layout(), 0, 1, &frameSet,
                                            0, nullptr);
                    boundOpaque = true;
                }
                gpu::DepthPointPush push{};
                push.model = draw.model;
                push.lightSlot = slot;
                pointPipeline_.pushConstants(cmd, &push, sizeof(push));
                draw.mesh->bindAndDraw(cmd);
            }
            if (anyMasked) {
                pointMaskedPipeline_.bind(cmd);
                VkDescriptorSet sets[2] = { frameSet, materialSet };
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pointMaskedPipeline_.layout(), 0, 2, sets,
                                        0, nullptr);
                for (const DrawItem& draw : submission.draws) {
                    if (classifyCaster(draw, materials) != CasterKind::Masked ||
                        !inRange(draw))
                        continue;
                    gpu::DepthPointMaskedPush push{};
                    push.model = draw.model;
                    push.materialIndex = draw.materialIndex;
                    push.lightSlot = slot;
                    pointMaskedPipeline_.pushConstants(cmd, &push, sizeof(push));
                    draw.mesh->bindAndDraw(cmd);
                }
            }
        }
        vkCmdEndRendering(cmd);
    }

    // Unrendered cube slots hold garbage in UNDEFINED-discarded layers, but
    // the transition below covers the WHOLE image, and the shader only ever
    // samples slots prepare() assigned this frame.
    {
        VkImageMemoryBarrier2 barriers[2] = {
            rhi::imageBarrier(sunShadow_.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
                              VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                              VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            rhi::imageBarrier(pointShadow_.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
                              VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                              VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        };
        rhi::cmdBarrier(cmd, barriers, 2);
    }
}

void ShadowPass::shutdown() {
    if (!device_)
        return;
    sunPipeline_.destroy();
    pointPipeline_.destroy();
    sunMaskedPipeline_.destroy();
    pointMaskedPipeline_.destroy();
    for (VkImageView& view : pointShadowFaceViews_) {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device_->device(), view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (compareSampler2D_ != VK_NULL_HANDLE)
        vkDestroySampler(device_->device(), compareSampler2D_, nullptr);
    if (compareSamplerCube_ != VK_NULL_HANDLE)
        vkDestroySampler(device_->device(), compareSamplerCube_, nullptr);
    if (rawSampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_->device(), rawSampler_, nullptr);
    compareSampler2D_ = VK_NULL_HANDLE;
    compareSamplerCube_ = VK_NULL_HANDLE;
    rawSampler_ = VK_NULL_HANDLE;
    sunShadow_.destroy();
    pointShadow_.destroy();
    device_ = nullptr;
}

} // namespace renderer
