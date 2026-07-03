#include "Renderer/Renderer.h"

#include "Engine/Screenshot.h"
#include "RHI/Barrier.h"
#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"
#include "RHI/Swapchain.h"

#include <glm/gtc/matrix_inverse.hpp>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_vulkan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>

namespace renderer {

namespace {

using StatsClock = std::chrono::steady_clock;

float msBetween(StatsClock::time_point from, StatsClock::time_point to) {
    return std::chrono::duration<float, std::milli>(to - from).count();
}

void smooth(float& average, float sample) {
    average += 0.08f * (sample - average);
}

constexpr uint32_t kInitialMaterialCapacity = 256;

} // namespace

void Renderer::init(rhi::Device& device, rhi::Swapchain& swapchain,
                    rhi::ShaderCompiler& shaderCompiler) {
    device_ = &device;
    swapchain_ = &swapchain;
    shaderCompiler_ = &shaderCompiler;

    VkSemaphoreTypeCreateInfo timelineType{};
    timelineType.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineType.initialValue = 0;
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = &timelineType;
    VK_CHECK(vkCreateSemaphore(device_->device(), &semInfo, nullptr, &frameTimeline_));

    createFrameContexts();
    createSceneTarget();

    materials_.init(device);
    createFrameSetLayout();

    const uint32_t viewMask = (1u << viewCount_) - 1u;
    shadowPass_.init(device, shaderCompiler, frameSetLayout_);
    forwardPass_.init(device, shaderCompiler, kSceneColorFormat, kSceneDepthFormat,
                      viewMask, frameSetLayout_, materials_.setLayout());
    skyboxPass_.init(device, shaderCompiler, kSceneColorFormat, kSceneDepthFormat,
                     viewMask, frameSetLayout_);

    tonemapTargetFormat_ = swapchain_->format();
    tonemapPass_.init(device, shaderCompiler, tonemapTargetFormat_);
}

void Renderer::createFrameContexts() {
    for (FrameContext& frame : frames_) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = device_->graphicsQueueFamily();
        VK_CHECK(vkCreateCommandPool(device_->device(), &poolInfo, nullptr, &frame.commandPool));

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device_->device(), &allocInfo, &frame.commandBuffer));

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(device_->device(), &semInfo, nullptr, &frame.acquireSemaphore));

        // Growable per-frame descriptor allocator, wholesale-reset at slot
        // reuse; passes allocate freely without predicting counts.
        frame.descriptors.init(*device_, 64);

        rhi::BufferDesc uboDesc{};
        uboDesc.size = sizeof(gpu::FrameData);
        uboDesc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        uboDesc.memory = rhi::MemoryUsage::HostUpload;
        uboDesc.debugName = "frame data";
        frame.frameUbo.create(*device_, uboDesc);

        rhi::BufferDesc lightsDesc{};
        lightsDesc.size = sizeof(gpu::PointLightData) * kMaxPointLights;
        lightsDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        lightsDesc.memory = rhi::MemoryUsage::HostUpload;
        lightsDesc.debugName = "point lights";
        frame.lightsBuffer.create(*device_, lightsDesc);

        rhi::BufferDesc materialsDesc{};
        materialsDesc.size = sizeof(gpu::MaterialData) * kInitialMaterialCapacity;
        materialsDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        materialsDesc.memory = rhi::MemoryUsage::HostUpload;
        materialsDesc.debugName = "materials";
        frame.materialsBuffer.create(*device_, materialsDesc);
    }
}

void Renderer::createSceneTarget() {
    sceneExtent_ = swapchain_->extent();

    rhi::TextureDesc colorDesc{};
    colorDesc.format = kSceneColorFormat;
    colorDesc.extent = sceneExtent_;
    colorDesc.arrayLayers = viewCount_;
    colorDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    // 2D_ARRAY view even at 1 layer: multiview attachments address layers by
    // view index, and mono must not be a special case.
    colorDesc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    colorDesc.debugName = "scene color (HDR)";
    sceneColor_.create(*device_, colorDesc);

    rhi::TextureDesc depthDesc = colorDesc;
    depthDesc.format = kSceneDepthFormat;
    depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthDesc.debugName = "scene depth";
    sceneDepth_.create(*device_, depthDesc);
}

void Renderer::createFrameSetLayout() {
    VkDescriptorSetLayoutBinding bindings[9]{};
    for (uint32_t i = 0; i < 9; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // sun raw
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // point raw

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 9;
    layoutInfo.pBindings = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(device_->device(), &layoutInfo, nullptr,
                                         &frameSetLayout_));
}

uint32_t Renderer::buildFrameData(const FrameSubmission& submission,
                                  gpu::FrameData& frame,
                                  std::vector<gpu::PointLightData>& lights) const {
    for (uint32_t v = 0; v < kMaxViews; ++v) {
        // Mono renders view 0 only, but both slots stay valid so nothing may
        // ever depend on a single-view assumption.
        const ViewCamera& cam =
            submission.views[std::min(v, uint32_t(viewCount_ - 1))];
        const glm::mat4 viewProj = cam.proj * cam.view;
        frame.views[v].viewProj = viewProj;
        frame.views[v].invViewProj = glm::inverse(viewProj);
        frame.views[v].cameraPos = glm::vec4(cam.position, 1.0f);
    }

    frame.sunDirection = glm::vec4(glm::normalize(submission.sun.direction), 0.0f);
    frame.sunColor = glm::vec4(submission.sun.color, submission.sun.intensity);
    frame.ambientColor = glm::vec4(glm::vec3(submission.ambient), 0.0f);
    frame.flags = 0;
    if (submission.shadowsEnabled)
        frame.flags |= SV_FRAME_SHADOWS_ENABLED;
    if (submission.sun.enabled)
        frame.flags |= SV_FRAME_SUN_ENABLED;
    if (submission.softShadows)
        frame.flags |= SV_FRAME_SOFT_SHADOWS;
    frame.sunPenumbraScale =
        std::tan(glm::radians(0.5f * std::max(submission.sun.angularSizeDeg, 0.0f)));

    // Shadow matrices + cube-array slot assignment.
    const std::vector<int> slots = shadowPass_.prepare(submission, frame);

    lights.clear();
    uint32_t shadowedCount = 0;
    const size_t lightCount =
        std::min<size_t>(submission.pointLights.size(), kMaxPointLights);
    for (size_t i = 0; i < lightCount; ++i) {
        const PointLightState& state = submission.pointLights[i];
        gpu::PointLightData data{};
        data.position = glm::vec4(state.position, state.intensity);
        data.color = glm::vec4(state.color, 0.0f);
        data.attenLinear = state.attenLinear;
        data.attenQuadratic = state.attenQuadratic;
        data.shadowIndex = slots[i];
        data.radius = std::max(state.radius, 0.0f);
        if (slots[i] >= 0)
            shadowedCount = std::max(shadowedCount, uint32_t(slots[i]) + 1);
        lights.push_back(data);
    }
    frame.pointLightCount = static_cast<uint32_t>(lights.size());
    return shadowedCount;
}

void Renderer::uploadFrameBuffers(FrameContext& frame, const gpu::FrameData& frameData,
                                  const std::vector<gpu::PointLightData>& lights) {
    frame.frameUbo.upload(&frameData, sizeof(frameData));
    if (!lights.empty())
        frame.lightsBuffer.upload(lights.data(),
                                  lights.size() * sizeof(gpu::PointLightData));

    const std::vector<gpu::MaterialData>& materials = materials_.materials();
    const VkDeviceSize materialBytes =
        std::max<VkDeviceSize>(materials.size(), 1) * sizeof(gpu::MaterialData);
    if (frame.materialsBuffer.size() < materialBytes) {
        // This slot's previous submission has already retired (renderFrame
        // waits on the timeline before touching the slot), so recreating the
        // buffer here is safe.
        rhi::BufferDesc desc{};
        desc.size = materialBytes * 2;
        desc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        desc.memory = rhi::MemoryUsage::HostUpload;
        desc.debugName = "materials";
        frame.materialsBuffer.create(*device_, desc);
    }
    if (!materials.empty())
        frame.materialsBuffer.upload(materials.data(),
                                     materials.size() * sizeof(gpu::MaterialData));
}

VkDescriptorSet Renderer::writeFrameSet(FrameContext& frame,
                                        const std::vector<gpu::PointLightData>& lights) {
    VkDescriptorSet set = frame.descriptors.allocate(frameSetLayout_);
    rhi::DescriptorWriter{}
        .writeBuffer(0, frame.frameUbo.handle(), 0, sizeof(gpu::FrameData),
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .writeBuffer(1, frame.lightsBuffer.handle(), 0,
                     std::max<VkDeviceSize>(lights.size(), 1) *
                         sizeof(gpu::PointLightData),
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeBuffer(2, frame.materialsBuffer.handle(), 0, frame.materialsBuffer.size(),
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeImage(3, shadowPass_.sunShadowView(), shadowPass_.compareSampler2D(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(4, shadowPass_.pointShadowView(), shadowPass_.compareSamplerCube(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(5, skyboxPass_.cubemapView(), skyboxPass_.sampler(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(6, skyboxPass_.equirectView(), skyboxPass_.sampler(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(7, shadowPass_.sunShadowView(), shadowPass_.rawSampler(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(8, shadowPass_.pointShadowView(), shadowPass_.rawSampler(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .flush(device_->device(), set);
    return set;
}

rhi::PresentResult Renderer::renderFrame(const FrameSubmission& submission,
                                         ImDrawData* uiDrawData) {
    pollScreenshot(false);

    FrameContext& frame = frames_[frameSlot_];

    const StatsClock::time_point frameStart = StatsClock::now();

    // Block until this slot's previous submission retired.
    if (frame.submittedTimelineValue != 0) {
        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &frameTimeline_;
        waitInfo.pValues = &frame.submittedTimelineValue;
        VK_CHECK(vkWaitSemaphores(device_->device(), &waitInfo, UINT64_MAX));
    }
    const StatsClock::time_point afterSlotWait = StatsClock::now();
    smooth(frameStats_.slotWaitMs, msBetween(frameStart, afterSlotWait));

    const uint32_t imageIndex = swapchain_->acquireImage(frame.acquireSemaphore);
    if (imageIndex == UINT32_MAX)
        return rhi::PresentResult::OutOfDate;
    const StatsClock::time_point afterAcquire = StatsClock::now();
    smooth(frameStats_.acquireMs, msBetween(afterSlotWait, afterAcquire));

    // Arm the screenshot copy for this frame if one was requested.
    bool captureThisFrame = false;
    if (screenshot_.state == Screenshot::State::Requested) {
        screenshot_.extent = swapchain_->extent();
        screenshot_.format = swapchain_->format();
        const VkDeviceSize bytes =
            VkDeviceSize(screenshot_.extent.width) * screenshot_.extent.height * 4;
        if (!screenshot_.readback.valid() || screenshot_.readback.size() < bytes) {
            rhi::BufferDesc desc{};
            desc.size = bytes;
            desc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            desc.memory = rhi::MemoryUsage::HostReadback;
            desc.debugName = "screenshot readback";
            screenshot_.readback.create(*device_, desc);
        }
        captureThisFrame = true;
    }

    VK_CHECK(vkResetCommandPool(device_->device(), frame.commandPool, 0));
    frame.descriptors.reset();

    gpu::FrameData frameData{};
    std::vector<gpu::PointLightData> lights;
    const uint32_t shadowedLightCount = buildFrameData(submission, frameData, lights);
    uploadFrameBuffers(frame, frameData, lights);
    VkDescriptorSet frameSet = writeFrameSet(frame, lights);

    recordFrame(frame, imageIndex, submission, frameSet, shadowedLightCount,
                uiDrawData, captureThisFrame);

    VkSemaphoreSubmitInfo waitAcquire{};
    waitAcquire.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitAcquire.semaphore = frame.acquireSemaphore;
    // First swapchain-image access this frame is the UNDEFINED->attachment
    // transition feeding the tonemap+UI pass.
    waitAcquire.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signals[2]{};
    signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[0].semaphore = frameTimeline_;
    signals[0].value = ++timelineValue_;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[1].semaphore = swapchain_->renderFinishedSemaphore(imageIndex);
    signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = frame.commandBuffer;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitAcquire;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 2;
    submit.pSignalSemaphoreInfos = signals;
    VK_CHECK(vkQueueSubmit2(device_->graphicsQueue(), 1, &submit, VK_NULL_HANDLE));
    frame.submittedTimelineValue = timelineValue_;

    if (captureThisFrame) {
        screenshot_.timelineValue = timelineValue_;
        screenshot_.state = Screenshot::State::InFlight;
    }

    frameSlot_ = (frameSlot_ + 1) % kFramesInFlight;

    const StatsClock::time_point beforePresent = StatsClock::now();
    const rhi::PresentResult result = swapchain_->present(device_->graphicsQueue(), imageIndex);
    smooth(frameStats_.presentMs, msBetween(beforePresent, StatsClock::now()));
    return result;
}

void Renderer::recordFrame(FrameContext& frame, uint32_t imageIndex,
                           const FrameSubmission& submission, VkDescriptorSet frameSet,
                           uint32_t shadowedLightCount, ImDrawData* uiDrawData,
                           bool captureThisFrame) {
    VkCommandBuffer cmd = frame.commandBuffer;
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // ---- Pass 1: shadow casters (owns its targets + transitions) ----
    shadowPass_.record(cmd, frameSet, submission, shadowedLightCount);

    // Scene target: previous contents are irrelevant (UNDEFINED discard).
    {
        VkImageMemoryBarrier2 barriers[2] = {
            rhi::imageBarrier(sceneColor_.image(), VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
            rhi::imageBarrier(sceneDepth_.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
                              VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL),
        };
        rhi::cmdBarrier(cmd, barriers, 2);
    }

    // ---- Pass 2+3: forward PBR then skybox, one multiview scene pass ----
    {
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = sceneColor_.view();
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color.float32[0] = 0.0f;
        color.clearValue.color.float32[1] = 0.0f;
        color.clearValue.color.float32[2] = 0.0f;
        color.clearValue.color.float32[3] = 1.0f;

        VkRenderingAttachmentInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = sceneDepth_.view();
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.clearValue.depthStencil.depth = 0.0f; // reverse-Z far

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = sceneExtent_;
        rendering.layerCount = 1;
        rendering.viewMask = (1u << viewCount_) - 1u;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = &depth;
        vkCmdBeginRendering(cmd, &rendering);

        VkViewport viewport{};
        viewport.width = static_cast<float>(sceneExtent_.width);
        viewport.height = static_cast<float>(sceneExtent_.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = sceneExtent_;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        forwardPass_.record(cmd, frameSet, materials_.set(), submission);
        skyboxPass_.record(cmd, frameSet, submission.sky);

        vkCmdEndRendering(cmd);
    }

    // ---- Pass 4: tonemap resolve + UI in one backbuffer pass ----
    VkImage backbuffer = swapchain_->image(imageIndex);
    {
        VkImageMemoryBarrier2 barriers[2] = {
            // Scene color: attachment -> sampled by the tonemap fragment.
            rhi::imageBarrier(sceneColor_.image(), VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            // Backbuffer: fresh attachment (chained to the acquire semaphore
            // at COLOR_ATTACHMENT_OUTPUT).
            rhi::imageBarrier(backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
        };
        rhi::cmdBarrier(cmd, barriers, 2);

        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = swapchain_->imageView(imageIndex);
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // The fullscreen tonemap overwrites every pixel; ImGui blends on top
        // within the same pass (primitive order guarantees the blend sees the
        // tonemapped scene).
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = swapchain_->extent();
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &rendering);

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchain_->extent().width);
        viewport.height = static_cast<float>(swapchain_->extent().height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = swapchain_->extent();
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkDescriptorSet sceneSet = frame.descriptors.allocate(tonemapPass_.sceneSetLayout());
        rhi::DescriptorWriter{}
            .writeImage(0, sceneColor_.view(), tonemapPass_.sampler(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .flush(device_->device(), sceneSet);
        tonemapPass_.record(cmd, sceneSet, tonemapSettings_, 0);

        if (uiDrawData)
            ImGui_ImplVulkan_RenderDrawData(uiDrawData, cmd);

        vkCmdEndRendering(cmd);
    }

    // ---- Optional screenshot copy, then hand the image to the presenter ----
    if (captureThisFrame) {
        rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
            backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));

        VkBufferImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { screenshot_.extent.width, screenshot_.extent.height, 1 };
        VkCopyImageToBufferInfo2 copy{};
        copy.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        copy.srcImage = backbuffer;
        copy.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copy.dstBuffer = screenshot_.readback.handle();
        copy.regionCount = 1;
        copy.pRegions = &region;
        vkCmdCopyImageToBuffer2(cmd, &copy);

        VkBufferMemoryBarrier2 hostRead = rhi::bufferBarrier(
            screenshot_.readback.handle(),
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        VkImageMemoryBarrier2 toPresent = rhi::imageBarrier(
            backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_NONE, 0,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        rhi::cmdBarrier(cmd, &toPresent, 1, &hostRead, 1);
    } else {
        rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
            backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, 0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));
    }

    VK_CHECK(vkEndCommandBuffer(cmd));
}

bool Renderer::requestScreenshot(const std::string& path) {
    if (screenshot_.state != Screenshot::State::Idle) {
        screenshotStatus_ = "capture already in progress";
        return false;
    }
    if (!swapchain_->supportsCapture()) {
        screenshotStatus_ = "surface does not support reading the backbuffer";
        return false;
    }
    screenshot_.path = path;
    screenshot_.state = Screenshot::State::Requested;
    screenshotStatus_ = "capturing...";
    return true;
}

void Renderer::pollScreenshot(bool blockUntilDone) {
    if (screenshot_.state != Screenshot::State::InFlight)
        return;
    if (blockUntilDone) {
        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &frameTimeline_;
        waitInfo.pValues = &screenshot_.timelineValue;
        VK_CHECK(vkWaitSemaphores(device_->device(), &waitInfo, UINT64_MAX));
    } else {
        uint64_t counter = 0;
        VK_CHECK(vkGetSemaphoreCounterValue(device_->device(), frameTimeline_, &counter));
        if (counter < screenshot_.timelineValue)
            return;
    }
    finishScreenshot();
}

void Renderer::finishScreenshot() {
    screenshot_.readback.invalidate();
    const uint32_t width = screenshot_.extent.width;
    const uint32_t height = screenshot_.extent.height;
    const uint8_t* src = static_cast<const uint8_t*>(screenshot_.readback.mapped());

    bool swapRB;
    switch (screenshot_.format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
        swapRB = true;
        break;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
        swapRB = false;
        break;
    default:
        screenshotStatus_ = "unsupported swapchain format for capture";
        screenshot_.readback.destroy();
        screenshot_.state = Screenshot::State::Idle;
        return;
    }

    // Rows come out of the copy top-to-bottom (no GL-style flip needed);
    // drop alpha and order channels RGB for the PNG writer.
    std::vector<unsigned char> rgb(size_t(width) * height * 3);
    for (size_t px = 0; px < size_t(width) * height; ++px) {
        const uint8_t* in = src + px * 4;
        rgb[px * 3 + 0] = swapRB ? in[2] : in[0];
        rgb[px * 3 + 1] = in[1];
        rgb[px * 3 + 2] = swapRB ? in[0] : in[2];
    }

    const bool ok = Engine::Screenshot::writePNG(screenshot_.path,
                                                 static_cast<int>(width),
                                                 static_cast<int>(height), 3, rgb.data());
    if (ok) {
        screenshotStatus_ = "saved " + screenshot_.path;
        std::cout << "Screenshot saved: " << screenshot_.path << "\n";
    } else {
        screenshotStatus_ = "failed to write " + screenshot_.path;
        std::cerr << "Failed to save screenshot: " << screenshot_.path << "\n";
    }
    screenshot_.readback.destroy();
    screenshot_.state = Screenshot::State::Idle;
}

void Renderer::onSwapchainRecreated() {
    createSceneTarget();
    // Deterministic surface-format selection keeps this stable; guard anyway
    // so a format flip rebuilds the tonemap pipeline instead of misrendering.
    if (swapchain_->format() != tonemapTargetFormat_) {
        tonemapTargetFormat_ = swapchain_->format();
        tonemapPass_.init(*device_, *shaderCompiler_, tonemapTargetFormat_);
    }
}

void Renderer::destroyFrameContexts() {
    for (FrameContext& frame : frames_) {
        frame.frameUbo.destroy();
        frame.lightsBuffer.destroy();
        frame.materialsBuffer.destroy();
        frame.descriptors.destroy();
        if (frame.acquireSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(device_->device(), frame.acquireSemaphore, nullptr);
        if (frame.commandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_->device(), frame.commandPool, nullptr);
        frame.acquireSemaphore = VK_NULL_HANDLE;
        frame.commandPool = VK_NULL_HANDLE;
        frame.commandBuffer = VK_NULL_HANDLE;
        frame.submittedTimelineValue = 0;
    }
}

void Renderer::shutdown() {
    if (!device_)
        return;
    device_->waitIdle();

    // The GPU is idle, so an in-flight capture has valid data — finish it
    // rather than dropping the user's screenshot on exit.
    pollScreenshot(true);
    if (screenshot_.state == Screenshot::State::Requested)
        screenshotStatus_ = "app closed before the capture frame";
    screenshot_.readback.destroy();
    screenshot_.state = Screenshot::State::Idle;

    tonemapPass_.shutdown();
    skyboxPass_.shutdown();
    forwardPass_.shutdown();
    shadowPass_.shutdown();
    if (frameSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device_->device(), frameSetLayout_, nullptr);
    frameSetLayout_ = VK_NULL_HANDLE;
    materials_.shutdown();
    sceneColor_.destroy();
    sceneDepth_.destroy();
    destroyFrameContexts();
    if (frameTimeline_ != VK_NULL_HANDLE)
        vkDestroySemaphore(device_->device(), frameTimeline_, nullptr);
    frameTimeline_ = VK_NULL_HANDLE;
    device_ = nullptr;
    swapchain_ = nullptr;
    shaderCompiler_ = nullptr;
}

} // namespace renderer
