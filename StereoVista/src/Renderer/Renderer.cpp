#include "Renderer/Renderer.h"

#include "Core/Profiling.h"
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
    // Async-compute chain timelines (created even when the device has no
    // distinct compute queue: waits on their initial value 0 are free and the
    // submit code stays branch-light).
    VK_CHECK(vkCreateSemaphore(device_->device(), &semInfo, nullptr, &computeTimeline_));
    VK_CHECK(vkCreateSemaphore(device_->device(), &semInfo, nullptr, &uploadTimeline_));

    // One aligned FrameData slice per viewport in each slot's frame UBO (the
    // slices differ only in views[] — see uploadFrameBuffers).
    const VkDeviceSize uboAlign =
        device.properties().limits.minUniformBufferOffsetAlignment;
    frameUboStride_ = (sizeof(gpu::FrameData) + uboAlign - 1) & ~(uboAlign - 1);

    createFrameContexts();
    createSceneTargets();

    uploadRing_.create(device, kUploadRingBytes, "streaming upload ring");

    materials_.init(device);
    createFrameSetLayout();

    const uint32_t viewMask = (1u << viewCount_) - 1u;
    shadowPass_.init(device, shaderCompiler, frameSetLayout_, materials_.setLayout());
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
        allocInfo.commandBufferCount = 2;
        VkCommandBuffer graphicsCmds[2] = {};
        VK_CHECK(vkAllocateCommandBuffers(device_->device(), &allocInfo, graphicsCmds));
        frame.preCommandBuffer = graphicsCmds[0];
        frame.commandBuffer = graphicsCmds[1];

        // Compute pool on the compute queue family (== the graphics family on
        // single-queue devices — the buffer then simply never records).
        poolInfo.queueFamilyIndex = device_->computeQueueFamily();
        VK_CHECK(vkCreateCommandPool(device_->device(), &poolInfo, nullptr,
                                     &frame.computePool));
        allocInfo.commandPool = frame.computePool;
        allocInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device_->device(), &allocInfo,
                                          &frame.computeCommandBuffer));

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(device_->device(), &semInfo, nullptr, &frame.acquireSemaphore));

        // Growable per-frame descriptor allocator, wholesale-reset at slot
        // reuse; passes allocate freely without predicting counts.
        frame.descriptors.init(*device_, 64);

        rhi::BufferDesc uboDesc{};
        uboDesc.size = frameUboStride_ * kMaxViewports; // one slice per viewport
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

void Renderer::destroySceneDepthLayerViews(uint32_t viewport) {
    for (VkImageView& view : viewports_[viewport].sceneDepthLayerViews) {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device_->device(), view, nullptr);
        view = VK_NULL_HANDLE;
    }
}

void Renderer::createSceneTargets() {
    for (uint32_t v = 0; v < kMaxViewports; ++v) {
        ViewportTarget& vp = viewports_[v];
        // The old per-layer views point at the depth image create() is about
        // to free — drop them first. Viewports beyond the count just release.
        destroySceneDepthLayerViews(v);
        vp.sceneColor.destroy();
        vp.sceneDepth.destroy();
        if (v >= viewportCount_) {
            vp.extent = { 0, 0 };
            continue;
        }

        // XR: the (single) scene target follows the HMD eye resolution.
        // Docked: each viewport follows its requested panel extent. Classic:
        // the window.
        vp.extent = (sceneExtentOverride_.width && sceneExtentOverride_.height)
                        ? sceneExtentOverride_
                    : viewportOutput_ ? viewportExtentWant_[v]
                                      : swapchain_->extent();
        vp.extent.width = std::max(vp.extent.width, 1u);
        vp.extent.height = std::max(vp.extent.height, 1u);

        rhi::TextureDesc colorDesc{};
        colorDesc.format = kSceneColorFormat;
        colorDesc.extent = vp.extent;
        colorDesc.arrayLayers = viewCount_;
        colorDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        // 2D_ARRAY view even at 1 layer: multiview attachments address layers
        // by view index, and mono must not be a special case.
        colorDesc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        colorDesc.debugName = "scene color (HDR)";
        vp.sceneColor.create(*device_, colorDesc);

        rhi::TextureDesc depthDesc = colorDesc;
        depthDesc.format = kSceneDepthFormat;
        // TRANSFER_SRC: depth-picking rect copies (Phase 6).
        depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        depthDesc.debugName = "scene depth";
        vp.sceneDepth.create(*device_, depthDesc);

        // Per-eye single-layer depth views for the resolve attachment.
        for (uint32_t e = 0; e < viewCount_; ++e)
            vp.sceneDepthLayerViews[e] = vp.sceneDepth.createLayerView(
                VK_IMAGE_VIEW_TYPE_2D, e, 1, "scene depth layer");
    }
}

void Renderer::destroySceneTargets() {
    for (uint32_t v = 0; v < kMaxViewports; ++v) {
        destroySceneDepthLayerViews(v);
        viewports_[v].sceneColor.destroy();
        viewports_[v].sceneDepth.destroy();
        viewports_[v].extent = { 0, 0 };
    }
}

void Renderer::onSceneExtentChanged() {
    createSceneTargets();
    // Size-dependent point-cloud buffers follow the scene targets (device is
    // idle here; the next prepare() recreates them at the new extents).
    pointCloudPass_.onSwapchainRecreated();
    // The published depth samples referenced the old extent; drop them so
    // picking never reconstructs from a stale rectangle set.
    depthResult_ = DepthReadback{};
    for (FrameContext& frame : frames_)
        frame.pendingDepth = DepthReadback{};
}

void Renderer::createGuiTargets() {
    // Same format as the swapchain, so the tonemap/overlay pipelines (built
    // against tonemapTargetFormat_) render into them unchanged and the GUI's
    // copy to the backbuffer is value-preserving. One layer per swapchain
    // layer: quad-buffer stereo resolves each eye into its own layer, and the
    // per-layer GUI pass shows the matching eye in every viewport image (the
    // GUI itself stays at zero parallax).
    const uint32_t layers = std::max(swapchain_->layers(), 1u);
    for (uint32_t v = 0; v < viewportCount_; ++v) {
        ViewportTarget& vp = viewports_[v];
        vp.guiLayers = layers;
        rhi::TextureDesc desc{};
        desc.format = tonemapTargetFormat_;
        desc.extent = viewportExtentWant_[v];
        desc.arrayLayers = layers;
        desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.debugName = "viewport color (GUI image)";
        vp.guiColor.create(*device_, desc);
        for (uint32_t layer = 0; layer < layers; ++layer) {
            vp.guiLayerViews[layer] = vp.guiColor.createLayerView(
                VK_IMAGE_VIEW_TYPE_2D, layer, 1, "viewport color layer");
            vp.guiImguiSets[layer] = ImGui_ImplVulkan_AddTexture(
                tonemapPass_.sampler(), vp.guiLayerViews[layer],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        // Fresh image (UNDEFINED): force one render before the GUI samples it.
        vp.everRendered = false;
    }
}

void Renderer::destroyGuiTargets(bool releaseImguiSets) {
    for (ViewportTarget& vp : viewports_) {
        for (VkDescriptorSet& set : vp.guiImguiSets) {
            if (set != VK_NULL_HANDLE && releaseImguiSets)
                ImGui_ImplVulkan_RemoveTexture(set);
            set = VK_NULL_HANDLE;
        }
        for (VkImageView& view : vp.guiLayerViews) {
            if (view != VK_NULL_HANDLE)
                vkDestroyImageView(device_->device(), view, nullptr);
            view = VK_NULL_HANDLE;
        }
        vp.guiColor.destroy();
        vp.guiLayers = 0;
        vp.everRendered = false;
    }
}

void Renderer::setViewportOutputs(const VkExtent2D* extents, uint32_t count) {
    // XR owns the scene target (HMD eye resolution); the app re-applies its
    // desktop config after endXR.
    if (xrActive_)
        return;
    count = std::min(count, kMaxViewports);
    const bool enable = count > 0 && extents != nullptr;
    const uint32_t newCount = enable ? count : 1;

    // Desired per-viewport extents (>= 1 px); the classic path is exactly one
    // viewport at the swapchain extent with no GUI texture.
    VkExtent2D want[kMaxViewports]{};
    for (uint32_t v = 0; v < newCount; ++v) {
        want[v] = enable ? extents[v] : swapchain_->extent();
        want[v].width = std::max(want[v].width, 1u);
        want[v].height = std::max(want[v].height, 1u);
    }

    const uint32_t guiLayersWanted = std::max(swapchain_->layers(), 1u);
    bool same = (enable == viewportOutput_) && (newCount == viewportCount_);
    for (uint32_t v = 0; same && v < newCount; ++v)
        same = want[v].width == viewports_[v].extent.width &&
               want[v].height == viewports_[v].extent.height &&
               (!enable || viewports_[v].guiLayers == guiLayersWanted);
    if (same)
        return; // called every frame — the no-op path must stay free

    device_->waitIdle(); // rare: a config change or a settled panel resize
    viewportOutput_ = enable;
    viewportCount_ = newCount;
    for (uint32_t v = 0; v < kMaxViewports; ++v)
        viewportExtentWant_[v] = (v < newCount) ? want[v] : VkExtent2D{ 0, 0 };
    destroyGuiTargets(true);
    if (enable)
        createGuiTargets();
    onSceneExtentChanged();
}

void Renderer::buildEyeLayout(VkExtent2D extent, EyeResolve out[kMaxViews],
                              uint32_t& outCount) const {
    const uint32_t w = extent.width;
    const uint32_t h = extent.height;
    if (viewCount_ < 2) {
        out[0] = { { { 0, 0 }, { w, h } }, 0, 0, true };
        outCount = 1;
        return;
    }
    outCount = 2;
    // The docked-viewport texture mirrors the swapchain's layer count, so this
    // layout applies to both targets: backbufferLayer doubles as the viewport-
    // texture layer when docked.
    if (swapchain_->layers() >= 2) {
        // Quad-buffer: each full-window eye resolves into its own target
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
    // Rebuild the view-count-dependent GPU state: the layered scene targets and
    // the multiview scene-pass pipelines. The backbuffer tonemap/overlay
    // pipelines stay non-multiview, so they are untouched.
    createSceneTargets();
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
    // XR takes over the scene target: drop the docked-viewport output (the
    // app re-applies its desktop config after endXR).
    if (viewportOutput_) {
        viewportOutput_ = false;
        viewportCount_ = 1;
        for (VkExtent2D& want : viewportExtentWant_)
            want = { 0, 0 };
        destroyGuiTargets(true);
    }
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

    // Guard the normalize: a zero direction (transient panel input) would put
    // NaNs into the whole lighting path.
    const glm::vec3 sunDir =
        glm::dot(submission.sun.direction, submission.sun.direction) > 1e-12f
            ? glm::normalize(submission.sun.direction)
            : glm::vec3(0.0f, -1.0f, 0.0f);
    frame.sunDirection = glm::vec4(sunDir, 0.0f);
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
                                  const std::vector<gpu::PointLightData>& lights,
                                  const FrameSubmission& submission) {
    // One FrameData slice per viewport: identical except views[], which holds
    // that viewport's per-eye cameras — the scene shaders then run unchanged
    // (they index views[gl_ViewIndex]) whichever viewport's set is bound.
    frame.frameUbo.upload(&frameData, sizeof(frameData), 0);
    if (viewportCount_ > 1) {
        gpu::FrameData viewportData = frameData;
        for (uint32_t v = 1; v < viewportCount_; ++v) {
            const ViewCamera* cams = viewportViews(submission, v);
            for (uint32_t e = 0; e < kMaxViews; ++e) {
                const ViewCamera& cam = cams[std::min(e, uint32_t(viewCount_ - 1))];
                const glm::mat4 viewProj = cam.proj * cam.view;
                viewportData.views[e].viewProj = viewProj;
                viewportData.views[e].invViewProj = glm::inverse(viewProj);
                viewportData.views[e].cameraPos = glm::vec4(cam.position, 1.0f);
            }
            frame.frameUbo.upload(&viewportData, sizeof(viewportData),
                                  v * frameUboStride_);
        }
    }
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
                                        const std::vector<gpu::PointLightData>& lights,
                                        VkDeviceSize frameUboOffset) {
    VkDescriptorSet set = frame.descriptors.allocate(frameSetLayout_);
    rhi::DescriptorWriter{}
        .writeBuffer(0, frame.frameUbo.handle(), frameUboOffset,
                     sizeof(gpu::FrameData), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
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
    SV_ZONE_N("beginFrameSlot");
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

    // Retire upload-ring space owned by frames the GPU has finished, and
    // destroy graveyard textures those frames could still have sampled.
    uint64_t completed = 0;
    VK_CHECK(vkGetSemaphoreCounterValue(device_->device(), frameTimeline_, &completed));
    uploadRing_.reclaim(completed);
    materials_.collectGarbage(completed);
}

uint64_t Renderer::completedFrameValue() const {
    uint64_t completed = 0;
    VK_CHECK(vkGetSemaphoreCounterValue(device_->device(), frameTimeline_, &completed));
    return completed;
}

void Renderer::buildRenderViewportMask(const FrameSubmission& submission,
                                       bool outRender[kMaxViewports]) const {
    for (uint32_t v = 0; v < kMaxViewports; ++v) {
        const bool hidden = v < submission.viewportCount && submission.viewportHidden[v];
        outRender[v] = v < viewportCount_ &&
                       (!hidden || !viewports_[v].everRendered);
    }
}

void Renderer::armDepthPick(FrameContext& frame, const FrameSubmission& submission,
                            const bool renderViewport[kMaxViewports]) {
    // The pick samples ONE viewport's depth (the one under the mouse). A
    // viewport skipped this frame keeps last frame's depth — don't pick from
    // it (recordScene records the copy only for rendered viewports).
    const uint32_t pickVp = std::min(submission.depthPickViewport, viewportCount_ - 1);
    if (!renderViewport[pickVp])
        return;
    const VkExtent2D pickExtent = viewports_[pickVp].extent;

    // Clamp the requested rects to the pick extent and size the slot's readback.
    std::vector<DepthQueryRect> depthRects;
    depthRects.reserve(submission.depthQueries.size());
    size_t texels = 0;
    for (const DepthQueryRect& req : submission.depthQueries) {
        DepthQueryRect rect = req;
        rect.origin = glm::max(rect.origin, glm::ivec2(0));
        rect.size.x = std::min(rect.size.x, int(pickExtent.width) - rect.origin.x);
        rect.size.y = std::min(rect.size.y, int(pickExtent.height) - rect.origin.y);
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
    frame.pendingDepth.viewport = pickVp;
    frame.pendingDepth.extent = pickExtent;
    const ViewCamera& cam0 = viewportViews(submission, pickVp)[0];
    frame.pendingDepth.invViewProj = glm::inverse(cam0.proj * cam0.view);
    frame.pendingDepth.cameraPos = cam0.position;
}

void Renderer::submitFrame(FrameContext& frame, bool computeActive, bool waitAcquire,
                           VkSemaphore renderFinished) {
    lastFrameAsyncCompute_ = computeActive;

    // Values for this frame's chain (class comment). priorComputeValue guards
    // the WAR hazard on the upload batch: its ring copies may rewrite
    // (resort-in-place) cloud ranges the PREVIOUS compute submission still
    // reads. uploadValue is signaled by the upload batch's transfer stages
    // and consumed by the compute batch (streamed cloud chunks staged this
    // frame flush in that batch).
    const uint64_t priorFrameValue = timelineValue_;
    const uint64_t priorComputeValue = computeTimelineValue_;
    const uint64_t uploadValue = ++uploadTimelineValue_;
    uint64_t computeWaitValue = priorComputeValue;

    // ---- Async compute submission ----
    // Submitted first, but it waits on uploadTimeline before its dispatches —
    // a wait the graphics submit below has yet to enqueue the signal for.
    // Timeline semaphores explicitly allow wait-before-signal, so the two
    // vkQueueSubmit2 calls may come in either order.
    if (computeActive) {
        computeWaitValue = ++computeTimelineValue_;

        VkSemaphoreSubmitInfo computeWaits[2]{};
        // Previous frame's graphics: its resolve fragments were the last
        // readers of the per-pixel buffers this submission clears.
        computeWaits[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        computeWaits[0].semaphore = frameTimeline_;
        computeWaits[0].value = priorFrameValue;
        computeWaits[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        // This frame's upload flush: the cloud geometry the dispatches read.
        // Only the dispatches wait — the dispatch-data copy and the clears
        // touch no ring destination and start immediately.
        computeWaits[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        computeWaits[1].semaphore = uploadTimeline_;
        computeWaits[1].value = uploadValue;
        computeWaits[1].stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        VkSemaphoreSubmitInfo computeSignal{};
        computeSignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        computeSignal.semaphore = computeTimeline_;
        computeSignal.value = computeWaitValue;
        computeSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkCommandBufferSubmitInfo computeCmd{};
        computeCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        computeCmd.commandBuffer = frame.computeCommandBuffer;

        VkSubmitInfo2 computeSubmit{};
        computeSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        computeSubmit.waitSemaphoreInfoCount = 2;
        computeSubmit.pWaitSemaphoreInfos = computeWaits;
        computeSubmit.commandBufferInfoCount = 1;
        computeSubmit.pCommandBufferInfos = &computeCmd;
        computeSubmit.signalSemaphoreInfoCount = 1;
        computeSubmit.pSignalSemaphoreInfos = &computeSignal;
        VK_CHECK(vkQueueSubmit2(device_->computeQueue(), 1, &computeSubmit,
                                VK_NULL_HANDLE));
    }

    // ---- Graphics: two batches in ONE vkQueueSubmit2 ----
    // Batch 0 (uploads + aux + shadow). Its semaphore ops touch only the
    // transfer stages, so the shadow raster work neither delays the upload
    // signal nor stalls on the previous compute.
    VkSemaphoreSubmitInfo preWait{};
    preWait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    preWait.semaphore = computeTimeline_;
    preWait.value = priorComputeValue;
    preWait.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;

    VkSemaphoreSubmitInfo preSignal{};
    preSignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    preSignal.semaphore = uploadTimeline_;
    preSignal.value = uploadValue;
    preSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;

    VkCommandBufferSubmitInfo preCmd{};
    preCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    preCmd.commandBuffer = frame.preCommandBuffer;

    // Batch 1 (scene + resolve + backbuffer). On the async path the compute
    // results are first read by the resolve fragments; on the inline path
    // the wait value is an already-signaled old one UNLESS the previous
    // frame ran async — then it orders this frame's inline clears/dispatches
    // after that compute work, which is why the inline stage mask also
    // covers CLEAR/COPY/COMPUTE.
    VkSemaphoreSubmitInfo sceneWaits[2]{};
    uint32_t sceneWaitCount = 0;
    if (waitAcquire) {
        // First swapchain-image access this frame is the UNDEFINED->attachment
        // transition feeding the tonemap+UI pass.
        sceneWaits[sceneWaitCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        sceneWaits[sceneWaitCount].semaphore = frame.acquireSemaphore;
        sceneWaits[sceneWaitCount].stageMask =
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ++sceneWaitCount;
    }
    sceneWaits[sceneWaitCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    sceneWaits[sceneWaitCount].semaphore = computeTimeline_;
    sceneWaits[sceneWaitCount].value = computeWaitValue;
    sceneWaits[sceneWaitCount].stageMask =
        computeActive ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                      : (VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_BIT |
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    ++sceneWaitCount;

    VkSemaphoreSubmitInfo sceneSignals[2]{};
    uint32_t sceneSignalCount = 0;
    sceneSignals[sceneSignalCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    sceneSignals[sceneSignalCount].semaphore = frameTimeline_;
    sceneSignals[sceneSignalCount].value = ++timelineValue_;
    sceneSignals[sceneSignalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    ++sceneSignalCount;
    if (renderFinished != VK_NULL_HANDLE) {
        sceneSignals[sceneSignalCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        sceneSignals[sceneSignalCount].semaphore = renderFinished;
        sceneSignals[sceneSignalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        ++sceneSignalCount;
    }

    VkCommandBufferSubmitInfo sceneCmd{};
    sceneCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    sceneCmd.commandBuffer = frame.commandBuffer;

    VkSubmitInfo2 graphicsSubmits[2]{};
    graphicsSubmits[0].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    graphicsSubmits[0].waitSemaphoreInfoCount = 1;
    graphicsSubmits[0].pWaitSemaphoreInfos = &preWait;
    graphicsSubmits[0].commandBufferInfoCount = 1;
    graphicsSubmits[0].pCommandBufferInfos = &preCmd;
    graphicsSubmits[0].signalSemaphoreInfoCount = 1;
    graphicsSubmits[0].pSignalSemaphoreInfos = &preSignal;
    graphicsSubmits[1].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    graphicsSubmits[1].waitSemaphoreInfoCount = sceneWaitCount;
    graphicsSubmits[1].pWaitSemaphoreInfos = sceneWaits;
    graphicsSubmits[1].commandBufferInfoCount = 1;
    graphicsSubmits[1].pCommandBufferInfos = &sceneCmd;
    graphicsSubmits[1].signalSemaphoreInfoCount = sceneSignalCount;
    graphicsSubmits[1].pSignalSemaphoreInfos = sceneSignals;
    VK_CHECK(vkQueueSubmit2(device_->graphicsQueue(), 2, graphicsSubmits,
                            VK_NULL_HANDLE));

    frame.submittedTimelineValue = timelineValue_;
    uploadRing_.notifySubmitted(timelineValue_);
}

rhi::PresentResult Renderer::renderFrame(const FrameSubmission& submission,
                                         ImDrawData* uiDrawData) {
    SV_ZONE_N("renderFrame");
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

    // Both pools: the slot's graphics retirement (waited in beginFrameSlot)
    // implies its compute submission retired too — the scene batch waited on
    // the compute timeline before its fragments ran.
    VK_CHECK(vkResetCommandPool(device_->device(), frame.commandPool, 0));
    VK_CHECK(vkResetCommandPool(device_->device(), frame.computePool, 0));
    frame.descriptors.reset();

    gpu::FrameData frameData{};
    std::vector<gpu::PointLightData> lights;
    const uint32_t shadowedLightCount = buildFrameData(submission, frameData, lights);
    uploadFrameBuffers(frame, frameData, lights, submission);
    // One per-frame set per viewport, differing only in the FrameData slice
    // (viewport cameras); the shadow pass and XR use frameSets[0].
    VkDescriptorSet frameSets[kMaxViewports]{};
    for (uint32_t v = 0; v < viewportCount_; ++v)
        frameSets[v] = writeFrameSet(frame, lights, v * frameUboStride_);

    // Which viewports render this frame (hidden GUI tabs are skipped, except
    // for a never-yet-rendered fresh target).
    bool renderViewport[kMaxViewports]{};
    buildRenderViewportMask(submission, renderViewport);

    // Point-cloud dispatch data for this frame (host upload; safe — this
    // slot's previous submission has retired). ONE flat view list covers
    // every rendered (viewport, eye) pair.
    PointCloudPass::ViewportInput pcViewports[kMaxViewports]{};
    for (uint32_t v = 0; v < viewportCount_; ++v) {
        pcViewports[v].extent =
            renderViewport[v] ? viewports_[v].extent : VkExtent2D{ 0, 0 };
        pcViewports[v].views = viewportViews(submission, v);
    }
    pointCloudPass_.prepare(submission, frameSlot_, pcViewports, viewportCount_,
                            viewCount_);

    // Overlay geometry for this frame (same slot-safety argument): one view
    // entry per (viewport, eye), indexed viewport * kMaxViews + eye.
    if (submission.overlay && !submission.overlay->empty()) {
        OverlayViewInfo views[kMaxViewports * kMaxViews];
        for (uint32_t v = 0; v < viewportCount_; ++v) {
            EyeResolve eyes[kMaxViews];
            uint32_t eyeCount = 0;
            buildEyeLayout(viewports_[v].extent, eyes, eyeCount);
            const ViewCamera* cams = viewportViews(submission, v);
            for (uint32_t e = 0; e < kMaxViews; ++e) {
                OverlayViewInfo& info = views[v * kMaxViews + e];
                const ViewCamera& cam = cams[std::min(e, uint32_t(viewCount_ - 1))];
                info.viewProj = cam.proj * cam.view;
                // Per-eye px sizing uses the ON-SCREEN rect (a half-width in
                // side-by-side) so overlay line widths / marker sizes stay in
                // real pixels.
                const VkExtent2D ext =
                    (e < eyeCount) ? eyes[e].rect.extent : viewports_[v].extent;
                info.viewportPx = glm::vec2(float(ext.width), float(ext.height));
                info.cameraPos = cam.position;
            }
        }
        overlayPass_.prepare(frameSlot_, *submission.overlay, views,
                             viewportCount_ * kMaxViews);
    } else {
        overlayPass_.prepare(frameSlot_, OverlayDrawList{}, nullptr, 0);
    }

    armDepthPick(frame, submission, renderViewport);

    // Record the three command buffers: uploads+shadow, the async compute
    // batch (when this frame takes the async path), then scene+backbuffer.
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(frame.preCommandBuffer, &beginInfo));
        recordPreScene(frame.preCommandBuffer, submission, frameSets[0],
                       shadowedLightCount);
        VK_CHECK(vkEndCommandBuffer(frame.preCommandBuffer));
    }
    const bool asyncCompute = recordAsyncCompute(frame);
    recordFrame(frame, imageIndex, submission, frameSets, renderViewport,
                asyncCompute, uiDrawData, captureThisFrame);

    submitFrame(frame, asyncCompute, /*waitAcquire=*/true,
                swapchain_->renderFinishedSemaphore(imageIndex));

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

    // Both pools — same slot-retirement argument as renderFrame.
    VK_CHECK(vkResetCommandPool(device_->device(), frame.commandPool, 0));
    VK_CHECK(vkResetCommandPool(device_->device(), frame.computePool, 0));
    frame.descriptors.reset();

    gpu::FrameData frameData{};
    std::vector<gpu::PointLightData> lights;
    const uint32_t shadowedLightCount = buildFrameData(submission, frameData, lights);
    uploadFrameBuffers(frame, frameData, lights, submission);
    VkDescriptorSet frameSet = writeFrameSet(frame, lights, 0);

    // XR is always exactly viewport 0 at the HMD eye extent.
    const VkExtent2D eyeExtent = viewports_[0].extent;
    PointCloudPass::ViewportInput pcViewport{ eyeExtent, submission.views };
    pointCloudPass_.prepare(submission, frameSlot_, &pcViewport, 1, viewCount_);

    // Overlay geometry: both eyes are full eye-resolution viewports.
    if (submission.overlay && !submission.overlay->empty()) {
        OverlayViewInfo views[kMaxViews];
        for (uint32_t v = 0; v < kMaxViews; ++v) {
            const ViewCamera& cam =
                submission.views[std::min(v, uint32_t(viewCount_ - 1))];
            views[v].viewProj = cam.proj * cam.view;
            views[v].viewportPx =
                glm::vec2(float(eyeExtent.width), float(eyeExtent.height));
            views[v].cameraPos = cam.position;
        }
        overlayPass_.prepare(frameSlot_, *submission.overlay, views, kMaxViews);
    } else {
        overlayPass_.prepare(frameSlot_, OverlayDrawList{}, nullptr, 0);
    }

    const bool renderViewport[kMaxViewports] = { true };
    armDepthPick(frame, submission, renderViewport);

    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(frame.preCommandBuffer, &beginInfo));
        recordPreScene(frame.preCommandBuffer, submission, frameSet, shadowedLightCount);
        VK_CHECK(vkEndCommandBuffer(frame.preCommandBuffer));
    }
    const bool asyncCompute = recordAsyncCompute(frame);
    recordFrameXR(frame, eyes, eyeSrgb, imageIndex, haveWindow, doMirror,
                  submission, frameSet, asyncCompute, uiDrawData);

    // The HMD eye images need no WSI semaphore (OpenXR sequences them via
    // acquire/wait/release); only the window mirror waits on its acquire and
    // signals its present-complete semaphore.
    submitFrame(frame, asyncCompute, /*waitAcquire=*/haveWindow,
                haveWindow ? swapchain_->renderFinishedSemaphore(imageIndex)
                           : VK_NULL_HANDLE);

    frameSlot_ = (frameSlot_ + 1) % kFramesInFlight;

    if (haveWindow)
        return swapchain_->present(device_->graphicsQueue(), imageIndex);
    return rhi::PresentResult::OutOfDate; // window swapchain needs a recreate
}

bool Renderer::asyncComputeSupported() const {
    return device_ != nullptr && device_->asyncComputeAvailable();
}

void Renderer::recordPreScene(VkCommandBuffer cmd, const FrameSubmission& submission,
                              VkDescriptorSet frameSet, uint32_t shadowedLightCount) {
    // ---- Pass 0: streaming uploads staged since the last frame ----
    uploadRing_.flush(cmd);

    // ---- Pass 0b: caller-recorded aux work (CursorPreview3D thumbnail) ----
    if (submission.recordAux)
        submission.recordAux(cmd);

    // ---- Pass 1: shadow casters (owns its targets + transitions) ----
    shadowPass_.record(cmd, frameSet, materials_.set(), materials_.materials(),
                       submission, shadowedLightCount);
}

bool Renderer::recordAsyncCompute(FrameContext& frame) {
    if (!(asyncComputeEnabled() && pointCloudPass_.active()))
        return false;
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(frame.computeCommandBuffer, &beginInfo));
    pointCloudPass_.recordCompute(frame.computeCommandBuffer, /*asyncQueue=*/true);
    VK_CHECK(vkEndCommandBuffer(frame.computeCommandBuffer));
    return true;
}

void Renderer::recordScene(VkCommandBuffer cmd, FrameContext& frame,
                           const FrameSubmission& submission,
                           const VkDescriptorSet* frameSets,
                           const bool renderViewport[kMaxViewports],
                           bool asyncCompute) {
    // ---- Pass 1b: point-cloud compute (Schütz rasterize / HQS) ----
    // Recorded ONCE for all viewports: the dispatches carry every rendered
    // (viewport, eye) view's section, so the point streams are decoded once
    // per frame however many viewports show them.
    // Async path: already recorded into the slot's compute command buffer
    // (recordAsyncCompute) and ordered against this batch by the compute
    // timeline. Fallback: record it inline, exactly where the single-queue
    // frame always had it — clears + dispatches + queue-scoped barriers; the
    // fullscreen resolves join the scene passes below either way. No-op when
    // the frame has no visible clouds.
    if (!asyncCompute)
        pointCloudPass_.recordCompute(cmd, /*asyncQueue=*/false);

    // One multiview scene pass per rendered viewport. Camera-independent work
    // (uploads, shadows, the compute above) already ran once; from here each
    // viewport only pays its own raster cost.
    const uint32_t pickViewport =
        std::min(submission.depthPickViewport, viewportCount_ - 1);
    for (uint32_t v = 0; v < viewportCount_; ++v) {
        if (!renderViewport[v])
            continue;
        ViewportTarget& vp = viewports_[v];

        // Scene target: previous contents are irrelevant (UNDEFINED discard).
        {
            VkImageMemoryBarrier2 barriers[2] = {
                rhi::imageBarrier(vp.sceneColor.image(), VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
                rhi::imageBarrier(vp.sceneDepth.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
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
            color.imageView = vp.sceneColor.view();
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.clearValue.color.float32[0] = 0.0f;
            color.clearValue.color.float32[1] = 0.0f;
            color.clearValue.color.float32[2] = 0.0f;
            color.clearValue.color.float32[3] = 1.0f;

            VkRenderingAttachmentInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth.imageView = vp.sceneDepth.view();
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            // STORE since Phase 6: the depth-picking copies and the resolve
            // pass's overlays both consume the scene depth after this pass.
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth.clearValue.depthStencil.depth = 0.0f; // reverse-Z far

            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.renderArea.extent = vp.extent;
            rendering.layerCount = 1;
            rendering.viewMask = (1u << viewCount_) - 1u;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &color;
            rendering.pDepthAttachment = &depth;
            vkCmdBeginRendering(cmd, &rendering);

            VkViewport viewport{};
            viewport.width = static_cast<float>(vp.extent.width);
            viewport.height = static_cast<float>(vp.extent.height);
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{};
            scissor.extent = vp.extent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            forwardPass_.record(cmd, frameSets[v], materials_.set(), submission);
            // Point clouds composite against the mesh depth (reverse-Z GREATER,
            // depth write) BEFORE the skybox so background pixels stay cheap.
            pointCloudPass_.recordResolve(cmd, v);
            skyboxPass_.record(cmd, frameSets[v], submission.sky);

            vkCmdEndRendering(cmd);
        }

        // ---- Pass 3b: depth-picking rect copies (pick viewport only) ----
        if (frame.pendingDepth.valid && v == pickViewport) {
            rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
                vp.sceneDepth.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
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
            copy.srcImage = vp.sceneDepth.image();
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
                vp.sceneDepth.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            rhi::cmdBarrier(cmd, &backToDepth, 1, &hostRead, 1);
        } else {
            // No copy for this viewport — still order the scene pass's depth
            // writes against the resolve pass's overlay depth tests.
            rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
                vp.sceneDepth.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL));
        }

        vp.everRendered = true;
    }
}

void Renderer::recordFrame(FrameContext& frame, uint32_t imageIndex,
                           const FrameSubmission& submission,
                           const VkDescriptorSet* frameSets,
                           const bool renderViewport[kMaxViewports],
                           bool asyncCompute, ImDrawData* uiDrawData,
                           bool captureThisFrame) {
    VkCommandBuffer cmd = frame.commandBuffer;
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // Scene passes ([inline point-cloud compute, once] -> per-viewport
    // multiview scene pass -> depth-pick copies), then the per-viewport
    // resolves below. Uploads + shadow ride the preceding batch
    // (frame.preCommandBuffer); queue-scoped barriers still order the two
    // batches on the graphics queue.
    recordScene(cmd, frame, submission, frameSets, renderViewport, asyncCompute);

    // ---- Pass 4: per-eye tonemap + overlay resolve, per viewport. ----
    // Classic path: exactly one viewport, resolved straight into the
    // backbuffer. Mono = 1 full-window eye; quad-buffer stereo = 2 full-window
    // eyes into the 2 swapchain layers (correct per-eye occlusion);
    // side-by-side = 2 half-window eyes squished into the single backbuffer
    // layer (overlays on top, since the full-res depth can't line up with the
    // squished image). Scene depth is attached in every case (the
    // tonemap/overlay pipelines declare the format).
    // Docked path: the SAME resolve renders into each viewport's offscreen
    // texture the GUI samples as an ImGui image (no extra pass — only the
    // target changes per viewport), and the backbuffer carries only the GUI.
    VkImage backbuffer = swapchain_->image(imageIndex);
    const bool toViewport = viewportOutput_;
    for (uint32_t v = 0; v < viewportCount_; ++v) {
        if (!renderViewport[v])
            continue;
        ViewportTarget& vp = viewports_[v];

        VkImageMemoryBarrier2 barriers[2] = {
            // Scene color (all layers): attachment -> sampled by the tonemap.
            rhi::imageBarrier(vp.sceneColor.image(), VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            // Resolve target: fresh attachment (previous contents discarded).
            // Backbuffer: chained to the acquire semaphore at
            // COLOR_ATTACHMENT_OUTPUT. Viewport texture: the srcStage
            // FRAGMENT_SHADER execution dependency orders the discard after
            // every prior UI submission that sampled it (frames overlap by
            // kFramesInFlight; same queue, so submission-order barriers cover
            // the backend's secondary-window draws too).
            toViewport
                ? rhi::imageBarrier(vp.guiColor.image(), VK_IMAGE_ASPECT_COLOR_BIT,
                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                : rhi::imageBarrier(backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
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
            .writeImage(0, vp.sceneColor.view(), tonemapPass_.sampler(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .flush(device_->device(), sceneSet);

        EyeResolve eyes[kMaxViews];
        uint32_t eyeCount = 0;
        buildEyeLayout(vp.extent, eyes, eyeCount);
        for (uint32_t e = 0; e < eyeCount; ++e) {
            const EyeResolve& eye = eyes[e];

            VkRenderingAttachmentInfo color{};
            color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color.imageView = toViewport
                                  ? vp.guiLayerViews[eye.backbufferLayer]
                                  : swapchain_->imageView(imageIndex, eye.backbufferLayer);
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            // The fullscreen tonemap overwrites every pixel of this eye's rect;
            // overlays blend on top within the same pass.
            color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingAttachmentInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth.imageView = vp.sceneDepthLayerViews[eye.sceneLayer];
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
            // Overlay view params are per (viewport, eye).
            overlayPass_.record(cmd, v * kMaxViews + eye.sceneLayer,
                                /*forceOnTop=*/!eye.overlayDepthTest);

            vkCmdEndRendering(cmd);
        }
    }

    // ---- Pass 5: ImGui on the backbuffer (no depth). Drawn once per swapchain
    //      layer so a stereo HUD lands on both eyes at zero parallax. Docked
    //      path: the backbuffer only now enters the frame (fresh attachment,
    //      cleared by loadOp — the dockspace covers it, the clear is the
    //      letterbox color) and the viewport texture moves to sampled for the
    //      GUI's image widget. ----
    if (toViewport) {
        VkImageMemoryBarrier2 barriers[kMaxViewports + 1];
        uint32_t barrierCount = 0;
        // Each rendered viewport texture (all layers): eye resolves -> sampled
        // by this frame's GUI pass (and, for a dragged-out panel, the
        // backend's later secondary-window submits on this queue). Skipped
        // viewports already sit in SHADER_READ_ONLY from their last frame.
        for (uint32_t v = 0; v < viewportCount_; ++v) {
            if (!renderViewport[v])
                continue;
            barriers[barrierCount++] =
                rhi::imageBarrier(viewports_[v].guiColor.image(),
                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        // Backbuffer (all layers): fresh attachment, chained to the
        // acquire semaphore at COLOR_ATTACHMENT_OUTPUT.
        barriers[barrierCount++] =
            rhi::imageBarrier(backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        rhi::cmdBarrier(cmd, barriers, barrierCount);
    } else if (uiDrawData) {
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
    }

    if (uiDrawData || toViewport) {
        // Quad-buffer + docked viewports: the GUI is identical on both layers
        // EXCEPT the viewport images, which must show that layer's eye. The
        // draw data is recorded once against the layer-0 (left-eye) sets, so
        // remap each viewport's ImTextureID to the current layer's set before
        // recording.
        auto remapViewportImage = [&](VkDescriptorSet from, VkDescriptorSet to) {
            if (!uiDrawData || from == to || from == VK_NULL_HANDLE)
                return;
            for (int list = 0; list < uiDrawData->CmdListsCount; ++list)
                for (ImDrawCmd& drawCmd : uiDrawData->CmdLists[list]->CmdBuffer)
                    if (drawCmd.TextureId == reinterpret_cast<ImTextureID>(from))
                        drawCmd.TextureId = reinterpret_cast<ImTextureID>(to);
        };
        auto remapAllViewportImages = [&](uint32_t fromLayer, uint32_t toLayer) {
            for (uint32_t v = 0; v < viewportCount_; ++v) {
                const ViewportTarget& vp = viewports_[v];
                if (fromLayer < vp.guiLayers && toLayer < vp.guiLayers)
                    remapViewportImage(vp.guiImguiSets[fromLayer],
                                       vp.guiImguiSets[toLayer]);
            }
        };

        for (uint32_t layer = 0; layer < swapchain_->layers(); ++layer) {
            if (toViewport && layer > 0)
                remapAllViewportImages(layer - 1, layer);

            VkRenderingAttachmentInfo color{};
            color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color.imageView = swapchain_->imageView(imageIndex, layer);
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            // Docked path: nothing rendered the backbuffer before the GUI —
            // clear it here (visible only where no GUI window covers it).
            color.loadOp = toViewport ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                      : VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.clearValue.color.float32[0] = 0.03f;
            color.clearValue.color.float32[1] = 0.03f;
            color.clearValue.color.float32[2] = 0.035f;
            color.clearValue.color.float32[3] = 1.0f;

            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.renderArea.extent = swapchain_->extent();
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &color;
            vkCmdBeginRendering(cmd, &rendering);
            // Each call cycles the backend's UI vertex-buffer ring, so the two
            // layer draws use independent buffers (ring sized for this in initImGui).
            if (uiDrawData)
                ImGui_ImplVulkan_RenderDrawData(uiDrawData, cmd);
            vkCmdEndRendering(cmd);
        }
        // Restore the left-eye bindings: the backend replays the same draw
        // data for dragged-out OS windows after this frame is submitted.
        if (toViewport && swapchain_->layers() > 1)
            remapAllViewportImages(swapchain_->layers() - 1, 0);
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
                             bool asyncCompute, ImDrawData* uiDrawData) {
    VkCommandBuffer cmd = frame.commandBuffer;
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // Same multiview scene pass as the desktop path (already 2-view); XR is
    // always exactly viewport 0 at the HMD eye extent.
    ViewportTarget& vp0 = viewports_[0];
    const VkDescriptorSet frameSets[kMaxViewports] = { frameSet };
    const bool renderViewport[kMaxViewports] = { true };
    recordScene(cmd, frame, submission, frameSets, renderViewport, asyncCompute);

    // Scene color (all layers): attachment -> sampled by the eye resolves.
    rhi::cmdImageBarrier(cmd, rhi::imageBarrier(
        vp0.sceneColor.image(), VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

    // One scene-color descriptor (the array view); each eye rides its layer in
    // the tonemap push constant, so both eyes and the mirror share this set.
    VkDescriptorSet sceneSet = frame.descriptors.allocate(tonemapPass_.sceneSetLayout());
    rhi::DescriptorWriter{}
        .writeImage(0, vp0.sceneColor.view(), tonemapPass_.sampler(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .flush(device_->device(), sceneSet);

    const VkRect2D eyeRect{ { 0, 0 }, vp0.extent };

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
        depth.imageView = vp0.sceneDepthLayerViews[e]; // per-eye scene depth
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
        viewport.width = float(vp0.extent.width);
        viewport.height = float(vp0.extent.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &eyeRect);

        // Output LINEAR into an sRGB eye target (hardware encodes) else encode here.
        tonemapPass_.record(cmd, sceneSet, tonemapSettings_, e, glm::vec2(0.0f),
                            glm::vec2(vp0.extent.width, vp0.extent.height),
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
    // Deterministic surface-format selection keeps this stable; guard anyway
    // so a format flip rebuilds the affected pipelines instead of misrendering.
    if (swapchain_->format() != tonemapTargetFormat_) {
        tonemapTargetFormat_ = swapchain_->format();
        tonemapPass_.init(*device_, *shaderCompiler_, tonemapTargetFormat_,
                          kSceneDepthFormat);
        overlayPass_.init(*device_, *shaderCompiler_, tonemapTargetFormat_,
                          kSceneDepthFormat, 0, kFramesInFlight);
    }
    // The viewport textures follow the swapchain's format AND layer count (a
    // stereo-mode flip changes layers); the device is idle here (recreate).
    if (viewportOutput_ &&
        (viewports_[0].guiColor.format() != tonemapTargetFormat_ ||
         viewports_[0].guiLayers != std::max(swapchain_->layers(), 1u))) {
        destroyGuiTargets(true);
        createGuiTargets();
    }
    onSceneExtentChanged();
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
        if (frame.computePool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_->device(), frame.computePool, nullptr);
        frame.acquireSemaphore = VK_NULL_HANDLE;
        frame.commandPool = VK_NULL_HANDLE;
        frame.preCommandBuffer = VK_NULL_HANDLE;
        frame.commandBuffer = VK_NULL_HANDLE;
        frame.computePool = VK_NULL_HANDLE;
        frame.computeCommandBuffer = VK_NULL_HANDLE;
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
    // The ImGui backend is torn down before the renderer; the viewport image
    // sets die with the application's descriptor pool, not via RemoveTexture.
    destroyGuiTargets(/*releaseImguiSets=*/false);
    destroySceneTargets();
    destroyFrameContexts();
    if (frameTimeline_ != VK_NULL_HANDLE)
        vkDestroySemaphore(device_->device(), frameTimeline_, nullptr);
    frameTimeline_ = VK_NULL_HANDLE;
    if (computeTimeline_ != VK_NULL_HANDLE)
        vkDestroySemaphore(device_->device(), computeTimeline_, nullptr);
    computeTimeline_ = VK_NULL_HANDLE;
    if (uploadTimeline_ != VK_NULL_HANDLE)
        vkDestroySemaphore(device_->device(), uploadTimeline_, nullptr);
    uploadTimeline_ = VK_NULL_HANDLE;
    device_ = nullptr;
    swapchain_ = nullptr;
    shaderCompiler_ = nullptr;
}

} // namespace renderer
