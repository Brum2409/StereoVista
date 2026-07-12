#include "Renderer/passes/PointCloudPass.h"

#include "RHI/Barrier.h"
#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>

namespace renderer {

namespace {

// Global sync2 memory barrier: the pass buffers are only ever reached through
// buffer device addresses, so one coarse barrier per stage boundary is both
// simplest and exactly as strong as per-buffer tracking here.
void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage,
                   VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                   VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

// Debug-utils label scopes (A.12) so RenderDoc/Nsight captures show the
// point-cloud work as named regions. volk leaves these entry points null
// when VK_EXT_debug_utils is not enabled (validation off) — guard and no-op.
void beginLabel(VkCommandBuffer cmd, const char* name) {
    if (!vkCmdBeginDebugUtilsLabelEXT)
        return;
    VkDebugUtilsLabelEXT label{};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name;
    vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
}

void endLabel(VkCommandBuffer cmd) {
    if (vkCmdEndDebugUtilsLabelEXT)
        vkCmdEndDebugUtilsLabelEXT(cmd);
}

} // namespace

void PointCloudPass::init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
                          VkFormat colorFormat, VkFormat depthFormat,
                          uint32_t viewMask, uint32_t framesInFlight) {
    shutdown();
    device_ = &device;

    // Each geometry kernel builds twice from one SPIR-V module: spec constant
    // 0 sizes the per-invocation view arrays (see pointcloud_common.glsl), so
    // the [0] variant compiles exactly as tight as the pre-multi-viewport
    // shaders and the [1] variant pays for 8 views only when actually bound.
    const uint32_t viewCaps[2] = { kMaxViews, kMaxViews * kMaxViewports };
    auto buildGeometryVariants = [&](rhi::Pipeline (&out)[2], const char* path,
                                     const char* name) {
        const std::vector<uint32_t> spirv = shaderCompiler.load(path);
        for (uint32_t variant = 0; variant < 2; ++variant)
            out[variant] = rhi::ComputePipelineBuilder{}
                               .setShader(spirv)
                               .setSpecConstant(0, viewCaps[variant])
                               .setDebugName(std::string(name) +
                                             (variant ? " (multi-viewport)" : ""))
                               .build(device);
    };
    buildGeometryVariants(rasterizePipelines_,
                          "assets/shaders_vk/pointcloud_rasterize.comp",
                          "pointcloud rasterize");
    buildGeometryVariants(hqsDepthPipelines_,
                          "assets/shaders_vk/pointcloud_hqs_depth.comp",
                          "pointcloud HQS depth");
    buildGeometryVariants(hqsColorPipelines_,
                          "assets/shaders_vk/pointcloud_hqs_color.comp",
                          "pointcloud HQS color");
    lookupPipeline_ =
        rhi::ComputePipelineBuilder{}
            .setShader(shaderCompiler.load("assets/shaders_vk/pointcloud_lookup.comp"))
            .setDebugName("pointcloud color lookup")
            .build(device);

    // Resolves composite inside the multiview scene pass: reverse-Z GREATER
    // with depth WRITE so points occlude/get occluded by meshes and later
    // passes (skybox) see the point depths.
    resolvePipeline_ =
        rhi::GraphicsPipelineBuilder{}
            .setShaders(shaderCompiler.load("assets/shaders_vk/fullscreen.vert"),
                        shaderCompiler.load("assets/shaders_vk/pointcloud_resolve.frag"))
            .setColorFormats({ colorFormat })
            .setDepthFormat(depthFormat)
            .setViewMask(viewMask)
            .setCullMode(VK_CULL_MODE_NONE)
            .setDepth(true, true, VK_COMPARE_OP_GREATER)
            .setDebugName("pointcloud resolve")
            .build(device);
    hqsResolvePipeline_ =
        rhi::GraphicsPipelineBuilder{}
            .setShaders(shaderCompiler.load("assets/shaders_vk/fullscreen.vert"),
                        shaderCompiler.load("assets/shaders_vk/pointcloud_hqs_resolve.frag"))
            .setColorFormats({ colorFormat })
            .setDepthFormat(depthFormat)
            .setViewMask(viewMask)
            .setCullMode(VK_CULL_MODE_NONE)
            .setDepth(true, true, VK_COMPARE_OP_GREATER)
            .setDebugName("pointcloud HQS resolve")
            .build(device);

    dispatchStaging_.resize(framesInFlight);
    dispatchDevice_.resize(framesInFlight);

    // Only 65535 workgroups per dispatch dimension are guaranteed; chunk huge
    // clouds (65k batches = 671M points) across several dispatches.
    maxGroupsX_ = device.properties().limits.maxComputeWorkGroupCount[0];
}

void PointCloudPass::ensureTargets(VkDeviceSize totalPixels) {
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    // (Re)creation only happens while the buffer is unused: either it does
    // not exist yet, or onSwapchainRecreated() destroyed it with the device
    // idle. Growing an existing buffer mid-flight would race frame N-1.
    auto ensure = [&](rhi::Buffer& buffer, VkDeviceSize bytes, const char* name) {
        if (buffer.valid() && buffer.size() >= bytes)
            return;
        buffer.destroy();
        rhi::BufferDesc desc{};
        desc.size = bytes;
        desc.usage = usage;
        desc.memory = rhi::MemoryUsage::GpuOnly;
        // Big long-lived per-pixel buffers (HQS accum is ~133 MB at 4K) get
        // their own VkDeviceMemory, same policy as PointCloudGpu (A.13).
        desc.dedicated = bytes >= (64ull << 20);
        // Written by the async compute queue, read by the resolve fragments
        // on the graphics queue — CONCURRENT sharing (also keeps the toggle
        // between the async and inline paths transfer-free).
        desc.shareGraphicsCompute = true;
        desc.debugName = name;
        buffer.create(*device_, desc);
    };

    // Each mode only allocates what it consumes (✎ the GL app allocated all
    // four buffers at init even for mesh-only scenes). Toggling modes creates
    // the other pair on that frame — creating a NEW buffer mid-frame is safe;
    // only growth in place would race the previous frame.
    if (hqsActive_) {
        ensure(hqsDepth_, totalPixels * sizeof(uint32_t), "pointcloud HQS depth");
        ensure(hqsAccum_, totalPixels * 4 * sizeof(uint32_t), "pointcloud HQS accum");
    } else {
        ensure(framebuffer_, totalPixels * sizeof(uint64_t), "pointcloud framebuffer");
        ensure(colorbuffer_, totalPixels * sizeof(uint32_t), "pointcloud colors");
    }
}

void PointCloudPass::prepare(const FrameSubmission& submission, uint32_t frameSlot,
                             const ViewportInput* viewports, uint32_t viewportCount,
                             uint32_t viewCount) {
    active_ = false;
    dispatches_.clear();
    lookupPushes_.clear();
    dispatchCopyBytes_ = 0;
    for (ResolveRecord& record : resolves_)
        record = ResolveRecord{};

    std::vector<const PointCloudDrawItem*> clouds;
    clouds.reserve(submission.pointClouds.size());
    for (const PointCloudDrawItem& item : submission.pointClouds)
        if (item.numBatches > 0 && item.addresses.batches != 0)
            clouds.push_back(&item);
    if (clouds.empty())
        return;

    // Hard cap: the packed 8-bit cloudID indexes the dispatch array in the
    // lookup pass, so an out-of-range id would be an out-of-bounds pointer
    // read. Extra clouds are dropped for the frame (the GL app rendered them
    // with wrong colours instead — not portable to BDA).
    if (clouds.size() > SV_PC_MAX_CLOUDS) {
        if (!warnedCloudCount_) {
            std::cerr << "[pointcloud] more than " << SV_PC_MAX_CLOUDS
                      << " point clouds in one frame; the extra clouds are "
                         "not rendered (widen the cloudID bit budget to fix)\n";
            warnedCloudCount_ = true;
        }
        clouds.resize(SV_PC_MAX_CLOUDS);
    }

    // Flatten (viewport, eye) into ONE view list with per-view extents and
    // per-pixel-buffer sections (element-offset prefix sums). Skipped
    // viewports (hidden GUI tab: extent {0,0}) contribute no views at all —
    // the geometry dispatches never project into them this frame.
    viewCount_ = std::max(viewCount, 1u);
    flatViews_.clear();
    VkDeviceSize totalPixels = 0;
    const uint32_t vpCount = std::min(viewportCount, kMaxViewports);
    for (uint32_t vp = 0; vp < vpCount; ++vp) {
        const ViewportInput& input = viewports[vp];
        const uint32_t pixels = input.extent.width * input.extent.height;
        if (pixels == 0 || !input.views)
            continue;
        for (uint32_t e = 0; e < viewCount_; ++e) {
            FlatView view;
            view.viewport = vp;
            view.extent = input.extent;
            view.camera = &input.views[std::min(e, kMaxViews - 1)];
            view.pixelOffset = totalPixels;
            view.pixels = pixels;
            flatViews_.push_back(view);
            totalPixels += pixels;
        }
    }
    totalViews_ = static_cast<uint32_t>(flatViews_.size());
    if (totalViews_ == 0 || totalViews_ > SV_PC_MAX_VIEWS)
        return; // > cap is unreachable (kMaxViewports * kMaxViews == the cap)

    const PointCloudSettings& settings = submission.pointCloudSettings;
    hqsActive_ = settings.hqs;
    ensureTargets(totalPixels);

    const int clipCount =
        static_cast<int>(std::min<size_t>(submission.clipPlanes.size(), SV_PC_CLIP_PLANES));
    const float hqsThreshold = std::max(settings.hqsThreshold, 0.0f);
    const int splatMaxRadius = std::max(settings.splatMaxRadius, 0);
    const float lodPointsPerPixel = std::max(settings.lodPointsPerPixel, 0.0f);

    // The inactive mode's addresses stay 0 — its shaders don't run and the
    // active mode's shaders never dereference the other mode's pointers.
    const VkDeviceAddress fbBase =
        framebuffer_.valid() ? framebuffer_.deviceAddress() : 0;
    const VkDeviceAddress colorBase =
        colorbuffer_.valid() ? colorbuffer_.deviceAddress() : 0;
    const VkDeviceAddress hqsDepthBase =
        hqsDepth_.valid() ? hqsDepth_.deviceAddress() : 0;
    const VkDeviceAddress hqsAccumBase =
        hqsAccum_.valid() ? hqsAccum_.deviceAddress() : 0;

    std::vector<gpu::PointCloudDispatch>& hostData = hostData_;
    hostData.clear();
    hostData.reserve(clouds.size() * totalViews_);

    for (size_t ci = 0; ci < clouds.size(); ++ci) {
        const PointCloudDrawItem& item = *clouds[ci];
        const uint32_t cloudID = static_cast<uint32_t>(ci); // capped above

        // World planes -> the cloud's local space, so the shader tests them
        // against the decoded local points directly (GL parity).
        glm::vec4 localClip[SV_PC_CLIP_PLANES] = {};
        if (clipCount > 0) {
            const glm::mat4 mt = glm::transpose(item.model);
            for (int i = 0; i < clipCount; ++i)
                localClip[i] = mt * submission.clipPlanes[i];
        }

        for (const FlatView& view : flatViews_) {
            const ViewCamera& cam = *view.camera;
            gpu::PointCloudDispatch d{};
            d.mvp = cam.proj * cam.view * item.model;
            d.modelView = cam.view * item.model;
            d.proj = cam.proj;
            for (int i = 0; i < clipCount; ++i)
                d.clipPlanes[i] = localClip[i];
            d.batches = item.addresses.batches;
            d.xyz4b = item.addresses.xyz4b;
            d.xyz8b = item.addresses.xyz8b;
            d.xyz12b = item.addresses.xyz12b;
            d.rgba = item.addresses.rgba;
            d.framebuffer = fbBase + view.pixelOffset * sizeof(uint64_t);
            d.colorbuffer = colorBase + view.pixelOffset * sizeof(uint32_t);
            d.hqsDepth = hqsDepthBase + view.pixelOffset * sizeof(uint32_t);
            d.hqsAccum = hqsAccumBase + view.pixelOffset * 4 * sizeof(uint32_t);
            d.imageWidth = static_cast<int>(view.extent.width);
            d.imageHeight = static_cast<int>(view.extent.height);
            d.pointsPerThread = item.pointsPerThread;
            d.cloudID = cloudID;
            d.splatMaxRadius = splatMaxRadius;
            d.clipPlaneCount = clipCount;
            d.hqsThreshold = hqsThreshold;
            d.lodPointsPerPixel = item.densityLod ? lodPointsPerPixel : 0.0f;
            hostData.push_back(d);
        }
    }

    // Write the dispatch structs into this slot's staging buffer; the shaders
    // read them from the slot's DEVICE-LOCAL copy (recorded by recordCompute)
    // — every geometry workgroup loads the whole struct and the lookup pass
    // dereferences it per pixel, so reading it straight from host-visible
    // memory would cross PCIe millions of times per frame. Growing/rewriting
    // the slot's buffers is safe: its previous submission has retired
    // (renderFrame waited on the timeline).
    rhi::Buffer& staging = dispatchStaging_[frameSlot];
    rhi::Buffer& deviceBuf = dispatchDevice_[frameSlot];
    const VkDeviceSize bytes = hostData.size() * sizeof(gpu::PointCloudDispatch);
    if (!staging.valid() || staging.size() < bytes) {
        rhi::BufferDesc desc{};
        desc.size = std::max<VkDeviceSize>(bytes * 2, 16 * 1024);
        desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        desc.memory = rhi::MemoryUsage::HostUpload;
        // The staging->device copy records on the async compute queue when
        // enabled and inline on the graphics queue when not; sharing spares
        // the pair a family-ownership hand-off on every toggle.
        desc.shareGraphicsCompute = true;
        desc.debugName = "pointcloud dispatch staging";
        staging.create(*device_, desc);
    }
    if (!deviceBuf.valid() || deviceBuf.size() < bytes) {
        rhi::BufferDesc desc{};
        desc.size = std::max<VkDeviceSize>(bytes * 2, 16 * 1024);
        desc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT; // + TRANSFER_DST (GpuOnly)
        desc.memory = rhi::MemoryUsage::GpuOnly;
        desc.shareGraphicsCompute = true; // same reason as the staging half
        desc.debugName = "pointcloud dispatch data";
        deviceBuf.create(*device_, desc);
    }
    staging.upload(hostData.data(), bytes);
    preparedSlot_ = frameSlot;
    dispatchCopyBytes_ = bytes;

    // ONE geometry dispatch record per cloud (single-pass multi-view): it
    // receives the (cloud, view 0) struct address and the shader strides to
    // the sibling views' structs itself — the dispatch array stays
    // cloud-major/view-minor for the lookup pass.
    const VkDeviceAddress dispatchBase = deviceBuf.deviceAddress();
    for (size_t ci = 0; ci < clouds.size(); ++ci) {
        DispatchRecord record;
        record.dispatchData =
            dispatchBase + ci * totalViews_ * sizeof(gpu::PointCloudDispatch);
        record.numBatches = clouds[ci]->numBatches;
        dispatches_.push_back(record);
    }

    // Per-view lookup pushes and per-viewport resolve pushes. A viewport's
    // eyes are CONSECUTIVE equal-sized flat views, so its resolve gets the
    // first eye's section as base + that viewport's pixel count — the shader
    // then offsets by gl_ViewIndex * pixelsPerView exactly as before.
    for (uint32_t fv = 0; fv < totalViews_; ++fv) {
        const FlatView& view = flatViews_[fv];
        const bool viewportFirstEye = (fv % viewCount_) == 0;

        if (!hqsActive_) {
            // One lookup dispatch per view; the shader walks the dispatch
            // array by cloudID (cloud-major layout: stride = totalViews).
            gpu::PointCloudLookupPush lookup{};
            lookup.framebuffer = fbBase + view.pixelOffset * sizeof(uint64_t);
            lookup.colorbuffer = colorBase + view.pixelOffset * sizeof(uint32_t);
            lookup.dispatchBase = dispatchBase + fv * sizeof(gpu::PointCloudDispatch);
            lookup.cloudStrideBytes =
                totalViews_ * static_cast<uint32_t>(sizeof(gpu::PointCloudDispatch));
            lookup.pixelCount = view.pixels;
            lookupPushes_.push_back(lookup);
        }

        if (!viewportFirstEye)
            continue;
        ResolveRecord& resolve = resolves_[view.viewport];
        resolve.active = true;
        if (hqsActive_) {
            resolve.hqsPush.hqsDepth =
                hqsDepthBase + view.pixelOffset * sizeof(uint32_t);
            resolve.hqsPush.hqsAccum =
                hqsAccumBase + view.pixelOffset * 4 * sizeof(uint32_t);
            for (uint32_t e = 0; e < kMaxViews; ++e) {
                const ViewCamera& cam =
                    *flatViews_[fv + std::min(e, viewCount_ - 1)].camera;
                // Window depth of linear eye depth d: -proj[2][2] + proj[3][2]/d.
                resolve.hqsPush.projAB[e] =
                    glm::vec4(cam.proj[2][2], cam.proj[3][2], 0.0f, 0.0f);
            }
            resolve.hqsPush.imageWidth = static_cast<int>(view.extent.width);
            resolve.hqsPush.imageHeight = static_cast<int>(view.extent.height);
            resolve.hqsPush.pixelsPerView = view.pixels;
        } else {
            resolve.push.framebuffer = fbBase + view.pixelOffset * sizeof(uint64_t);
            resolve.push.colorbuffer = colorBase + view.pixelOffset * sizeof(uint32_t);
            resolve.push.imageWidth = static_cast<int>(view.extent.width);
            resolve.push.imageHeight = static_cast<int>(view.extent.height);
            resolve.push.pixelsPerView = view.pixels;
        }
    }

    active_ = true;
}

void PointCloudPass::recordCompute(VkCommandBuffer cmd, bool asyncQueue) {
    if (!active_)
        return;
    beginLabel(cmd, hqsActive_ ? "pointcloud compute (HQS)" : "pointcloud compute");

    // Dispatch data: staging -> device-local. No wait needed before the copy
    // (the slot's previous submission retired before prepare() rewrote the
    // staging buffer); the CLEAR|COPY -> COMPUTE barrier below makes it
    // visible to the dispatches.
    if (dispatchCopyBytes_ > 0) {
        VkBufferCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        region.size = dispatchCopyBytes_;
        VkCopyBufferInfo2 copy{};
        copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copy.srcBuffer = dispatchStaging_[preparedSlot_].handle();
        copy.dstBuffer = dispatchDevice_[preparedSlot_].handle();
        copy.regionCount = 1;
        copy.pRegions = &region;
        vkCmdCopyBuffer2(cmd, &copy);
    }

    // Inline (single-queue) path only: the previous frame may still be
    // reading (resolve fragments) or writing (compute) these buffers — order
    // the clears after all of it. Barriers are queue-scoped, so this covers
    // the prior command buffer too. On the ASYNC queue this ordering is the
    // Renderer's job (the compute submission waits the previous frame's
    // graphics timeline value before anything here executes) — and the
    // FRAGMENT stage bit would be illegal on a compute-only family anyway.
    if (!asyncQueue)
        memoryBarrier(cmd,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    // Clear only what this frame's path consumes. Sentinel 0xFFFF... marks
    // "no point" in both the 64-bit framebuffer (fill repeats the 32-bit
    // pattern) and the HQS depth buffer.
    if (hqsActive_) {
        vkCmdFillBuffer(cmd, hqsDepth_.handle(), 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
        vkCmdFillBuffer(cmd, hqsAccum_.handle(), 0, VK_WHOLE_SIZE, 0u);
    } else {
        vkCmdFillBuffer(cmd, framebuffer_.handle(), 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
    }

    memoryBarrier(cmd,
                  VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    // One workgroup per batch covering EVERY view (the shader decodes each
    // point once and projects it per (viewport, eye)), chunked against
    // maxComputeWorkGroupCount[0].
    auto dispatchGeometry = [&](const rhi::Pipeline& pipeline) {
        pipeline.bind(cmd);
        for (const DispatchRecord& record : dispatches_) {
            for (uint32_t base = 0; base < record.numBatches; base += maxGroupsX_) {
                gpu::PointCloudComputePush push{};
                push.dispatchData = record.dispatchData;
                push.baseBatch = base;
                push.viewCount = totalViews_;
                pipeline.pushConstants(cmd, &push, sizeof(push));
                vkCmdDispatch(cmd, std::min(record.numBatches - base, maxGroupsX_), 1, 1);
            }
        }
    };

    const auto computeToCompute = [&] {
        memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };

    // Geometry variant: the small (kMaxViews) specialization whenever the
    // frame's flat view list fits one viewport's eyes — every single-viewport
    // frame, mono or stereo — else the full multi-viewport specialization.
    const uint32_t variant = totalViews_ <= kMaxViews ? 0u : 1u;
    if (hqsActive_) {
        // HQS: every cloud's depth dispatch must finish before ANY colour
        // dispatch (the colour pass reads the global nearest depth).
        dispatchGeometry(hqsDepthPipelines_[variant]);
        computeToCompute();
        dispatchGeometry(hqsColorPipelines_[variant]);
    } else {
        dispatchGeometry(rasterizePipelines_[variant]);
        computeToCompute();

        // Colour lookup: ONE dispatch per view resolves every pixel against
        // its owning cloud (cloudID -> dispatch-array pointer walk in the
        // shader) — the GL app needed a fullscreen dispatch per cloud here.
        // 2D grid (shader flattens row-major) so the group count stays under
        // maxComputeWorkGroupCount[0] even for 8K-class targets. Group counts
        // are per view: viewports differ in extent.
        lookupPipeline_.bind(cmd);
        for (const gpu::PointCloudLookupPush& push : lookupPushes_) {
            const uint32_t groups = (push.pixelCount + SV_PC_LOOKUP_WORKGROUP - 1) /
                                    SV_PC_LOOKUP_WORKGROUP;
            const uint32_t groupsX = std::min(std::max(groups, 1u), maxGroupsX_);
            const uint32_t groupsY = (groups + groupsX - 1) / groupsX;
            lookupPipeline_.pushConstants(cmd, &push, sizeof(push));
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        }
    }

    // Make the results visible to the fullscreen resolve fragments. On the
    // async queue the semaphore signal/wait pair (compute timeline -> scene
    // submission at FRAGMENT_SHADER) carries this dependency instead, with
    // full memory availability per the semaphore rules.
    if (!asyncQueue)
        memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    endLabel(cmd);
}

void PointCloudPass::recordResolve(VkCommandBuffer cmd, uint32_t viewportIndex) const {
    if (!active_ || viewportIndex >= kMaxViewports ||
        !resolves_[viewportIndex].active)
        return;
    const ResolveRecord& resolve = resolves_[viewportIndex];
    beginLabel(cmd, "pointcloud resolve");
    if (hqsActive_) {
        hqsResolvePipeline_.bind(cmd);
        hqsResolvePipeline_.pushConstants(cmd, &resolve.hqsPush,
                                          sizeof(resolve.hqsPush));
    } else {
        resolvePipeline_.bind(cmd);
        resolvePipeline_.pushConstants(cmd, &resolve.push, sizeof(resolve.push));
    }
    vkCmdDraw(cmd, 3, 1, 0, 0);
    endLabel(cmd);
}

void PointCloudPass::onSwapchainRecreated() {
    // Device idle (swapchain recreation) — drop the size-dependent buffers;
    // the next prepare() with clouds recreates them at the new extent.
    framebuffer_.destroy();
    colorbuffer_.destroy();
    hqsDepth_.destroy();
    hqsAccum_.destroy();
    active_ = false;
}

void PointCloudPass::shutdown() {
    if (!device_)
        return;
    for (uint32_t variant = 0; variant < 2; ++variant) {
        rasterizePipelines_[variant].destroy();
        hqsDepthPipelines_[variant].destroy();
        hqsColorPipelines_[variant].destroy();
    }
    lookupPipeline_.destroy();
    resolvePipeline_.destroy();
    hqsResolvePipeline_.destroy();
    framebuffer_.destroy();
    colorbuffer_.destroy();
    hqsDepth_.destroy();
    hqsAccum_.destroy();
    dispatchStaging_.clear();
    dispatchDevice_.clear();
    dispatches_.clear();
    active_ = false;
    device_ = nullptr;
}

} // namespace renderer
