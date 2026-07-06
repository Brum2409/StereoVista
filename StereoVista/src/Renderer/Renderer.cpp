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

    uploadRing_.create(device, kUploadRingBytes, "streaming upload ring");

    materials_.init(device);
    createFrameSetLayout();

    const uint32_t viewMask = (1u << viewCount_) - 1u;
    shadowPass_.init(device, shaderCompiler, frameSetLayout_);
    forwardPass_.init(device, shaderCompiler, kSceneColorFormat, kSceneDepthFormat,
                      viewMask, frameSetLayout_, materials_.setLayout());
    pointCloudPass_.init(device, shaderCompiler, kSceneColorFormat, kSceneDepthFormat,
                         viewMask, kFramesInFlight);
    skyboxPass_.init(device, shaderCompiler, kSceneColorFormat, kSceneDepthFormat,
                     viewMask, frameSetLayout_);

    tonemapTargetFormat_ = swapchain_->format();
    // Tonemap + overlays share one backbuffer pass carrying the scene depth
    // (overlays depth-test post-tonemap); both pipelines declare it.
    tonemapPass_.init(device, shaderCompiler, tonemapTargetFormat_, kSceneDepthFormat);
    overlayPass_.init(device, shaderCompiler, tonemapTargetFormat_, kSceneDepthFormat,
                      0 /* no multiview on the mono backbuffer */, kFramesInFlight);
}

// DepthReadback::sample() is defined inline in FrameSubmission.h (the cursor
// code shares the same type), so no out-of-line definition lives here.

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

void Renderer::destroySceneDepthLayerViews() {
    for (VkImageView& view : sceneDepthLayerViews_) {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device_->device(), view, nullptr);
        view = VK_NULL_HANDLE;
    }
}

void Renderer::createSceneTarget() {
    // The old per-layer views point at the depth image that create() is about
    // to free — drop them first.
    destroySceneDepthLayerViews();
    // In XR the scene target follows the HMD eye resolution, not the window.
    sceneExtent_ = (sceneExtentOverride_.width && sceneExtentOverride_.height)
                       ? sceneExtentOverride_
                       : swapchain_->extent();

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
    // TRANSFER_SRC: depth-picking rect copies (Phase 6).
    depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    depthDesc.debugName = "scene depth";
    sceneDepth_.create(*device_, depthDesc);

    // Per-eye single-layer depth views for the backbuffer resolve attachment.
    for (uint32_t v = 0; v < viewCount_; ++v)
        sceneDepthLayerViews_[v] =
            sceneDepth_.createLayerView(VK_IMAGE_VIEW_TYPE_2D, v, 1, "scene depth layer");
}

void Renderer::buildEyeLayout(EyeResolve out[kMaxViews], uint32_t& outCount) const {
    const uint32_t w = sceneExtent_.width;
    const uint32_t h = sceneExtent_.height;
    if (viewCount_ < 2) {
        out[0] = { { { 0, 0 }, { w, h } }, 0, 0, true };
        outCount = 1;
        return;
    }
    outCount = 2;
    if (swapchain_->layers() >= 2) {
        // Quad-buffer: each full-window eye resolves into its own swapchain
        // layer with its own scene depth (correct per-eye occlusion).
        out[0] = { { { 0, 0 }, { w, h } }, 0, 0, true };
        out[1] = { { { 0, 0 }, { w, h } }, 1, 1, true };
    } else {
        // Side-by-side into the single mono backbuffer layer. The scene depth is
        // full-res while the eye image is squished into a half, so overlays draw
        // on top rather than depth-test against a mismatched depth.
        const uint32_t halfW = w / 2;
        out[0] = { { { 0, 0 }, { halfW, h } }, 0, 0, false };
        out[1] = { { { static_cast<int32_t>(halfW), 0 }, { w - halfW, h } }, 1, 0, false };
    }
}

void Renderer::rebuildViewDependentState() {
    // Rebuild the view-count-dependent GPU state: the layered scene target and
    // the multiview scene-pass pipelines. The backbuffer tonemap/overlay
    // pipelines stay non-multiview, so they are untouched.
    createSceneTarget();
    const uint32_t viewMask = (1u << viewCount_) - 1u;
    forwardPass_.init(*device_, *shaderCompiler_, kSceneColorFormat, kSceneDepthFormat,
                      viewMask, frameSetLayout_, materials_.setLayout());
    // rebuildPipeline (not init) so the loaded sky textures survive the toggle.
    skyboxPass_.rebuildPipeline(*device_, *shaderCompiler_, kSceneColorFormat,
                                kSceneDepthFormat, viewMask, frameSetLayout_);
    // init rebuilds the pipelines (incl. the multiview resolve) and drops the
    // per-pixel buffers — recreated at the new ×viewCount size by the next prepare.
    pointCloudPass_.init(*device_, *shaderCompiler_, kSceneColorFormat, kSceneDepthFormat,
                         viewMask, kFramesInFlight);

    // Published depth samples referenced the old view set; drop them.
    depthResult_ = DepthReadback{};
    for (FrameContext& frame : frames_)
        frame.pendingDepth = DepthReadback{};
}

void Renderer::setViewCount(uint32_t count) {
    count = std::clamp(count, 1u, kMaxViews);
    if (count == viewCount_)
        return;
    device_->waitIdle();
    viewCount_ = count;
    rebuildViewDependentState();
}

void Renderer::beginXR(VkExtent2D eyeExtent) {
    device_->waitIdle();
    xrActive_ = true;
    sceneExtentOverride_ = eyeExtent; // scene target follows the HMD, not the window
    viewCount_ = 2;                   // both eyes, single multiview pass
    rebuildViewDependentState();      // always rebuilds (extent may change same-count)
}

void Renderer::endXR() {
    device_->waitIdle();
    xrActive_ = false;
    sceneExtentOverride_ = { 0, 0 }; // back to the window extent
    viewCount_ = 1;                  // the app re-applies its desktop stereo mode
    rebuildViewDependentState();
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

    // Section/clip planes (mesh.vert gl_ClipDistance; the point-cloud pass
    // consumes the same submission list separately, in cloud-local space).
    frame.clipPlaneCount = static_cast<uint32_t>(
        std::min<size_t>(submission.clipPlanes.size(), SV_MAX_CLIP_PLANES));
    for (uint32_t i = 0; i < frame.clipPlaneCount; ++i)
        frame.clipPlanes[i] = submission.clipPlanes[i];

    // Fragment (ring) cursor.
    const FragmentCursorState& cursor = submission.fragmentCursor;
    frame.showFragmentCursor = cursor.show ? 1u : 0u;
    frame.cursorPos = glm::vec4(cursor.position, cursor.valid ? 1.0f : 0.0f);
    frame.cursorOuterColor = cursor.outerColor;
    frame.cursorInnerColor = cursor.innerColor;
    frame.cursorRingParams = glm::vec4(cursor.outerRadius, cursor.outerThickness,
                                       cursor.innerRadius, cursor.innerThickness);

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

void Renderer::beginFrameSlot(FrameContext& frame) {
    const StatsClock::time_point start = StatsClock::now();

    // Block until this slot's previous submission retired.
    if (frame.submittedTimelineValue != 0) {
        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &frameTimeline_;
        waitInfo.pValues = &frame.submittedTimelineValue;
        VK_CHECK(vkWaitSemaphores(device_->device(), &waitInfo, UINT64_MAX));
    }
    smooth(frameStats_.slotWaitMs, msBetween(start, StatsClock::now()));

    // The slot's previous submission has retired — its depth-picking copy is
    // complete, so publish the result before anything overwrites the buffer.
    if (frame.pendingDepth.valid) {
        frame.depthReadback.invalidate();
        const float* src = static_cast<const float*>(frame.depthReadback.mapped());
        size_t texels = 0;
        for (const DepthQueryRect& rect : frame.pendingDepth.rects)
            texels += size_t(rect.size.x) * rect.size.y;
        frame.pendingDepth.depths.assign(src, src + texels);
        depthResult_ = std::move(frame.pendingDepth);
        frame.pendingDepth = DepthReadback{};
    }

    // Retire upload-ring space owned by frames the GPU has finished.
    uint64_t completed = 0;
    VK_CHECK(vkGetSemaphoreCounterValue(device_->device(), frameTimeline_, &completed));
    uploadRing_.reclaim(completed);
}

void Renderer::armDepthPick(FrameContext& frame, const FrameSubmission& submission) {
    // Clamp the requested rects to the scene extent and size the slot's readback.
    std::vector<DepthQueryRect> depthRects;
    depthRects.reserve(submission.depthQueries.size());
    size_t texels = 0;
    for (const DepthQueryRect& req : submission.depthQueries) {
        DepthQueryRect rect = req;
        rect.origin = glm::max(rect.origin, glm::ivec2(0));
        rect.size.x = std::min(rect.size.x, int(sceneExtent_.width) - rect.origin.x);
        rect.size.y = std::min(rect.size.y, int(sceneExtent_.height) - rect.origin.y);
        if (rect.size.x <= 0 || rect.size.y <= 0)
            continue;
        texels += size_t(rect.size.x) * rect.size.y;
        depthRects.push_back(rect);
    }
    if (texels == 0)
        return;

    const VkDeviceSize bytes = texels * sizeof(float);
    if (!frame.depthReadback.valid() || frame.depthReadback.size() < bytes) {
        rhi::BufferDesc desc{};
        desc.size = std::max<VkDeviceSize>(bytes * 2, 4 * 1024);
        desc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        desc.memory = rhi::MemoryUsage::HostReadback;
        desc.debugName = "depth picking readback";
        frame.depthReadback.create(*device_, desc);
    }
    frame.pendingDepth.valid = true;
    frame.pendingDepth.rects = std::move(depthRects);
    frame.pendingDepth.extent = sceneExtent_;
    const ViewCamera& cam0 = submission.views[0];
    frame.pendingDepth.invViewProj = glm::inverse(cam0.proj * cam0.view);
    frame.pendingDepth.cameraPos = cam0.position;
}

rhi::PresentResult Renderer::renderFrame(const FrameSubmission& submission,
                                         ImDrawData* uiDrawData) {
    pollScreenshot(false);

    FrameContext& frame = frames_[frameSlot_];
    beginFrameSlot(frame);

    const StatsClock::time_point afterSlotWait = StatsClock::now();
    const uint32_t imageIndex = swapchain_->acquireImage(frame.acquireSemaphore);
    if (imageIndex == UINT32_MAX)
        return rhi::PresentResult::OutOfDate;
    smooth(frameStats_.acquireMs, msBetween(afterSlotWait, StatsClock::now()));

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

    // Point-cloud dispatch data for this frame (host upload; safe — this
    // slot's previous submission has retired).
    pointCloudPass_.prepare(submission, frameSlot_, sceneExtent_, viewCount_);

    // Overlay geometry for this frame (same slot-safety argument).
    if (submission.overlay && !submission.overlay->empty()) {
        EyeResolve eyes[kMaxViews];
        uint32_t eyeCount = 0;
        buildEyeLayout(eyes, eyeCount);
        OverlayViewInfo views[kMaxViews];
        for (uint32_t v = 0; v < kMaxViews; ++v) {
            const ViewCamera& cam =
                submission.views[std::min(v, uint32_t(viewCount_ - 1))];
            views[v].viewProj = cam.proj * cam.view;
            // Per-eye px sizing uses the ON-SCREEN rect (a half-width in
            // side-by-side) so overlay line widths / marker sizes stay in
            // real pixels.
            const VkExtent2D ext = (v < eyeCount) ? eyes[v].rect.extent : sceneExtent_;
            views[v].viewportPx = glm::vec2(float(ext.width), float(ext.height));
            views[v].cameraPos = cam.position;
        }
        overlayPass_.prepare(frameSlot_, *submission.overlay, views, kMaxViews);
    } else {
        overlayPass_.prepare(frameSlot_, OverlayDrawList{}, nullptr, 0);
    }

    armDepthPick(frame, submission);

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
    uploadRing_.notifySubmitted(timelineValue_);

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

rhi::PresentResult Renderer::renderFrameXR(const FrameSubmission& submission,
                                           const XrEyeTarget eyes[2], bool eyeSrgb,
                                           bool mirrorToWindow, ImDrawData* uiDrawData) {
    pollScreenshot(false);

    FrameContext& frame = frames_[frameSlot_];
    beginFrameSlot(frame);

    // Always try to present the window (ImGui + optional mirror) so the GUI stays
    // usable to leave VR. If the window swapchain is out of date we still render
    // the HMD this frame; the caller recreates the window on the OutOfDate return.
    const uint32_t imageIndex = swapchain_->acquireImage(frame.acquireSemaphore);
    const bool haveWindow = (imageIndex != UINT32_MAX);
    const bool doMirror = haveWindow && mirrorToWindow;

    VK_CHECK(vkResetCommandPool(device_->device(), frame.commandPool, 0));
    frame.descriptors.reset();

    gpu::FrameData frameData{};
    std::vector<gpu::PointLightData> lights;
    const uint32_t shadowedLightCount = buildFrameData(submission, frameData, lights);
    uploadFrameBuffers(frame, frameData, lights);
    VkDescriptorSet frameSet = writeFrameSet(frame, lights);

    pointCloudPass_.prepare(submission, frameSlot_, sceneExtent_, viewCount_);

    // Overlay geometry: both eyes are full eye-resolution viewports.
    if (submission.overlay && !submission.overlay->empty()) {
        OverlayViewInfo views[kMaxViews];
        for (uint32_t v = 0; v < kMaxViews; ++v) {
            const ViewCamera& cam =
                submission.views[std::min(v, uint32_t(viewCount_ - 1))];
            views[v].viewProj = cam.proj * cam.view;
            views[v].viewportPx =
                glm::vec2(float(sceneExtent_.width), float(sceneExtent_.height));
            views[v].cameraPos = cam.position;
        }
        overlayPass_.prepare(frameSlot_, *submission.overlay, views, kMaxViews);
    } else {
        overlayPass_.prepare(frameSlot_, OverlayDrawList{}, nullptr, 0);
    }

    armDepthPick(frame, submission);

    recordFrameXR(frame, eyes, eyeSrgb, imageIndex, haveWindow, doMirror,
                  submission, frameSet, shadowedLightCount, uiDrawData);

    // The HMD eye images need no WSI semaphore (OpenXR sequences them via
    // acquire/wait/release); only the window mirror waits on its acquire and
    // signals its present-complete semaphore.
    VkSemaphoreSubmitInfo waitAcquire{};
    waitAcquire.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitAcquire.semaphore = frame.acquireSemaphore;
    waitAcquire.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signals[2]{};
    signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[0].semaphore = frameTimeline_;
    signals[0].value = ++timelineValue_;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    if (haveWindow) {
        signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signals[1].semaphore = swapchain_->renderFinishedSemaphore(imageIndex);
        signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = frame.commandBuffer;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = haveWindow ? 1u : 0u;
    submit.pWaitSemaphoreInfos = haveWindow ? &waitAcquire : nullptr;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = haveWindow ? 2u : 1u;
    submit.pSignalSemaphoreInfos = signals;
    VK_CHECK(vkQueueSubmit2(device_->graphicsQueue(), 1, &submit, VK_NULL_HANDLE));
    frame.submittedTimelineValue = timelineValue_;
    uploadRing_.notifySubmitted(timelineValue_);

    frameSlot_ = (frameSlot_ + 1) % kFramesInFlight;

    if (haveWindow)
        return swapchain_->present(device_->graphicsQueue(), imageIndex);
    return rhi::PresentResult::OutOfDate; // window swapchain needs a recreate
}

void Renderer::recordScene(VkCommandBuffer cmd, FrameContext& frame,
                           const FrameSubmission& submission, VkDescriptorSet frameSet,
                           uint32_t shadowedLightCount) {
    // ---- Pass 0: streaming uploads staged since the last frame ----
    uploadRing_.flush(cmd);

    // ---- Pass 0b: caller-recorded aux work (CursorPreview3D thumbnail) ----
    if (submission.recordAux)
        submission.recordAux(cmd);

    // ---- Pass 1: shadow casters (owns its targets + transitions) ----
    shadowPass_.record(cmd, frameSet, submission, shadowedLightCount);

    // ---- Pass 1b: point-cloud compute (Schütz rasterize / HQS) ----
    // Clears + dispatches + its own barriers; the fullscreen resolve joins
    // the scene pass below. No-op when the frame has no visible clouds.
    pointCloudPass_.recordCompute(cmd);

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
        // STORE since Phase 6: the depth-picking copies and the backbuffer
        // overlay pass both consume the scene depth after this pass.
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
        // Point clouds composite against the mesh depth (reverse-Z GREATER,
        // depth write) BEFORE the skybox so background pixels stay cheap.
        pointCloudPass_.recordResolve(cmd);
        skyboxPass_.record(cmd, frameSet, submission.sky);

        vkCmdEndRendering(cmd);
    }

    // ---- Pass 3b: depth-picking rect copies (scene depth -> readback) ----
    if (frame.pendingDepth.valid) {
        rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
            sceneDepth_.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));

        std::vector<VkBufferImageCopy2> regions;
        regions.reserve(frame.pendingDepth.rects.size());
        VkDeviceSize offset = 0;
        for (const DepthQueryRect& rect : frame.pendingDepth.rects) {
            VkBufferImageCopy2 region{};
            region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
            region.bufferOffset = offset;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            region.imageSubresource.baseArrayLayer = 0; // view 0
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { rect.origin.x, rect.origin.y, 0 };
            region.imageExtent = { uint32_t(rect.size.x), uint32_t(rect.size.y), 1 };
            regions.push_back(region);
            offset += VkDeviceSize(rect.size.x) * rect.size.y * sizeof(float);
        }
        VkCopyImageToBufferInfo2 copy{};
        copy.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        copy.srcImage = sceneDepth_.image();
        copy.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copy.dstBuffer = frame.depthReadback.handle();
        copy.regionCount = static_cast<uint32_t>(regions.size());
        copy.pRegions = regions.data();
        vkCmdCopyImageToBuffer2(cmd, &copy);

        VkBufferMemoryBarrier2 hostRead = rhi::bufferBarrier(
            frame.depthReadback.handle(),
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        VkImageMemoryBarrier2 backToDepth = rhi::imageBarrier(
            sceneDepth_.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        rhi::cmdBarrier(cmd, &backToDepth, 1, &hostRead, 1);
    } else {
        // No copy this frame — still order the scene pass's depth writes
        // against the overlay pass's depth tests below.
        rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
            sceneDepth_.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL));
    }
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

    // Shared scene passes (uploads -> shadow -> point-cloud compute -> multiview
    // scene pass -> depth-pick copies), then the backbuffer resolve below.
    recordScene(cmd, frame, submission, frameSet, shadowedLightCount);

    // ---- Pass 4: per-eye tonemap + overlay resolve into the backbuffer, then
    //      ImGui. Mono = 1 full-window eye; quad-buffer stereo = 2 full-window
    //      eyes into the 2 swapchain layers (correct per-eye occlusion);
    //      side-by-side = 2 half-window eyes squished into the single backbuffer
    //      layer (overlays on top, since the full-res depth can't line up with
    //      the squished image). Scene depth is attached in every case (the
    //      tonemap/overlay pipelines declare the format). ----
    VkImage backbuffer = swapchain_->image(imageIndex);
    {
        VkImageMemoryBarrier2 barriers[2] = {
            // Scene color (all layers): attachment -> sampled by the tonemap.
            rhi::imageBarrier(sceneColor_.image(), VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            // Backbuffer (all layers): fresh attachment (chained to the acquire
            // semaphore at COLOR_ATTACHMENT_OUTPUT).
            rhi::imageBarrier(backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
        };
        rhi::cmdBarrier(cmd, barriers, 2);

        // One scene-color descriptor (the array view); each eye's layer rides
        // the tonemap push constant, so both eyes share this set.
        VkDescriptorSet sceneSet = frame.descriptors.allocate(tonemapPass_.sceneSetLayout());
        rhi::DescriptorWriter{}
            .writeImage(0, sceneColor_.view(), tonemapPass_.sampler(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .flush(device_->device(), sceneSet);

        EyeResolve eyes[kMaxViews];
        uint32_t eyeCount = 0;
        buildEyeLayout(eyes, eyeCount);
        for (uint32_t e = 0; e < eyeCount; ++e) {
            const EyeResolve& eye = eyes[e];

            VkRenderingAttachmentInfo color{};
            color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color.imageView = swapchain_->imageView(imageIndex, eye.backbufferLayer);
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            // The fullscreen tonemap overwrites every pixel of this eye's rect;
            // overlays blend on top within the same pass.
            color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingAttachmentInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth.imageView = sceneDepthLayerViews_[eye.sceneLayer];
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.renderArea = eye.rect;
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &color;
            rendering.pDepthAttachment = &depth;
            vkCmdBeginRendering(cmd, &rendering);

            VkViewport viewport{};
            viewport.x = static_cast<float>(eye.rect.offset.x);
            viewport.y = static_cast<float>(eye.rect.offset.y);
            viewport.width = static_cast<float>(eye.rect.extent.width);
            viewport.height = static_cast<float>(eye.rect.extent.height);
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &eye.rect);

            tonemapPass_.record(cmd, sceneSet, tonemapSettings_, eye.sceneLayer,
                                glm::vec2(eye.rect.offset.x, eye.rect.offset.y),
                                glm::vec2(eye.rect.extent.width, eye.rect.extent.height));
            overlayPass_.record(cmd, eye.sceneLayer, /*forceOnTop=*/!eye.overlayDepthTest);

            vkCmdEndRendering(cmd);
        }
    }

    // ---- Pass 5: ImGui on the backbuffer (no depth). Drawn once per swapchain
    //      layer so a stereo HUD lands on both eyes at zero parallax. ----
    if (uiDrawData) {
        // Dynamic rendering has no implicit pass-to-pass dependency: order
        // the tonemap/overlay writes against ImGui's blended writes (all layers).
        rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
            backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));

        for (uint32_t layer = 0; layer < swapchain_->layers(); ++layer) {
            VkRenderingAttachmentInfo color{};
            color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color.imageView = swapchain_->imageView(imageIndex, layer);
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.renderArea.extent = swapchain_->extent();
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &color;
            vkCmdBeginRendering(cmd, &rendering);
            // Each call cycles the backend's UI vertex-buffer ring, so the two
            // layer draws use independent buffers (ring sized for this in initImGui).
            ImGui_ImplVulkan_RenderDrawData(uiDrawData, cmd);
            vkCmdEndRendering(cmd);
        }
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

void Renderer::recordFrameXR(FrameContext& frame, const XrEyeTarget eyes[2], bool eyeSrgb,
                             uint32_t windowImageIndex, bool haveWindow, bool doMirror,
                             const FrameSubmission& submission, VkDescriptorSet frameSet,
                             uint32_t shadowedLightCount, ImDrawData* uiDrawData) {
    VkCommandBuffer cmd = frame.commandBuffer;
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // Same multiview scene pass as the desktop path (already 2-view).
    recordScene(cmd, frame, submission, frameSet, shadowedLightCount);

    // Scene color (all layers): attachment -> sampled by the eye resolves.
    rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
        sceneColor_.image(), VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

    // One scene-color descriptor (the array view); each eye rides its layer in
    // the tonemap push constant, so both eyes and the mirror share this set.
    VkDescriptorSet sceneSet = frame.descriptors.allocate(tonemapPass_.sceneSetLayout());
    rhi::DescriptorWriter{}
        .writeImage(0, sceneColor_.view(), tonemapPass_.sampler(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .flush(device_->device(), sceneSet);

    const VkRect2D eyeRect{ { 0, 0 }, sceneExtent_ };

    // ---- Two HMD eyes: resolve each scene layer into its swapchain image ----
    for (uint32_t e = 0; e < 2; ++e) {
        // Runtime-owned image: discard prior contents (we overwrite every pixel)
        // and move to COLOR_ATTACHMENT_OPTIMAL — the layout OpenXR expects the
        // image to be in at xrReleaseSwapchainImage, so no post-transition.
        rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
            eyes[e].image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));

        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = eyes[e].view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // resolve overwrites all pixels
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingAttachmentInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = sceneDepthLayerViews_[e]; // per-eye scene depth
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea = eyeRect;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = &depth;
        vkCmdBeginRendering(cmd, &rendering);

        VkViewport viewport{};
        viewport.width = float(sceneExtent_.width);
        viewport.height = float(sceneExtent_.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &eyeRect);

        // Output LINEAR into an sRGB eye target (hardware encodes) else encode here.
        tonemapPass_.record(cmd, sceneSet, tonemapSettings_, e, glm::vec2(0.0f),
                            glm::vec2(sceneExtent_.width, sceneExtent_.height),
                            /*encodeSrgb=*/!eyeSrgb);
        overlayPass_.record(cmd, e, /*forceOnTop=*/false); // per-eye depth occlusion

        vkCmdEndRendering(cmd);
    }

    // ---- Window backbuffer: left-eye mirror (optional) behind ImGui ----
    if (haveWindow) {
        VkImage backbuffer = swapchain_->image(windowImageIndex);
        const VkExtent2D winExt = swapchain_->extent();
        rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
            backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));

        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = swapchain_->imageView(windowImageIndex);
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = doMirror ? VK_ATTACHMENT_LOAD_OP_DONT_CARE // mirror fills all
                                : VK_ATTACHMENT_LOAD_OP_CLEAR;    // no mirror: black
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color.float32[3] = 1.0f;

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = winExt;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &rendering);
        if (doMirror) {
            VkViewport viewport{};
            viewport.width = float(winExt.width);
            viewport.height = float(winExt.height);
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{ { 0, 0 }, winExt };
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            // Depth-less mirror of the left eye; window backbuffer is UNORM.
            tonemapPass_.recordMirror(cmd, sceneSet, tonemapSettings_, /*layer=*/0,
                                      glm::vec2(0.0f),
                                      glm::vec2(winExt.width, winExt.height),
                                      /*encodeSrgb=*/true);
        }
        vkCmdEndRendering(cmd);

        // ImGui on the window in its own no-depth pass (color barrier between).
        if (uiDrawData) {
            rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
                backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));

            VkRenderingAttachmentInfo ui{};
            ui.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            ui.imageView = swapchain_->imageView(windowImageIndex);
            ui.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ui.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            ui.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo uiPass{};
            uiPass.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            uiPass.renderArea.extent = winExt;
            uiPass.layerCount = 1;
            uiPass.colorAttachmentCount = 1;
            uiPass.pColorAttachments = &ui;
            vkCmdBeginRendering(cmd, &uiPass);
            ImGui_ImplVulkan_RenderDrawData(uiDrawData, cmd);
            vkCmdEndRendering(cmd);
        }

        // Window -> present.
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
    // Size-dependent point-cloud buffers follow the scene target (device is
    // idle here; the next prepare() recreates them at the new extent).
    pointCloudPass_.onSwapchainRecreated();
    // Deterministic surface-format selection keeps this stable; guard anyway
    // so a format flip rebuilds the affected pipelines instead of misrendering.
    if (swapchain_->format() != tonemapTargetFormat_) {
        tonemapTargetFormat_ = swapchain_->format();
        tonemapPass_.init(*device_, *shaderCompiler_, tonemapTargetFormat_,
                          kSceneDepthFormat);
        overlayPass_.init(*device_, *shaderCompiler_, tonemapTargetFormat_,
                          kSceneDepthFormat, 0, kFramesInFlight);
    }
    // The published depth samples referenced the old extent; drop them so
    // picking never reconstructs from a stale rectangle set.
    depthResult_ = DepthReadback{};
    for (FrameContext& frame : frames_)
        frame.pendingDepth = DepthReadback{};
}

void Renderer::destroyFrameContexts() {
    for (FrameContext& frame : frames_) {
        frame.frameUbo.destroy();
        frame.lightsBuffer.destroy();
        frame.materialsBuffer.destroy();
        frame.depthReadback.destroy();
        frame.pendingDepth = DepthReadback{};
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
    overlayPass_.shutdown();
    skyboxPass_.shutdown();
    pointCloudPass_.shutdown();
    forwardPass_.shutdown();
    shadowPass_.shutdown();
    if (frameSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device_->device(), frameSetLayout_, nullptr);
    frameSetLayout_ = VK_NULL_HANDLE;
    materials_.shutdown();
    uploadRing_.destroy();
    destroySceneDepthLayerViews();
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
