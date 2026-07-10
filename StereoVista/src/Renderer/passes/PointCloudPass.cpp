#include "Renderer/passes/PointCloudPass.h"

#include "RHI/Barrier.h"
#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"

#include <algorithm>
#include <cstring>
#include <iostream>

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

    rasterizePipeline_ =
        rhi::ComputePipelineBuilder{}
            .setShader(shaderCompiler.load("assets/shaders_vk/pointcloud_rasterize.comp"))
            .setDebugName("pointcloud rasterize")
            .build(device);
    lookupPipeline_ =
        rhi::ComputePipelineBuilder{}
            .setShader(shaderCompiler.load("assets/shaders_vk/pointcloud_lookup.comp"))
            .setDebugName("pointcloud color lookup")
            .build(device);
    hqsDepthPipeline_ =
        rhi::ComputePipelineBuilder{}
            .setShader(shaderCompiler.load("assets/shaders_vk/pointcloud_hqs_depth.comp"))
            .setDebugName("pointcloud HQS depth")
            .build(device);
    hqsColorPipeline_ =
        rhi::ComputePipelineBuilder{}
            .setShader(shaderCompiler.load("assets/shaders_vk/pointcloud_hqs_color.comp"))
            .setDebugName("pointcloud HQS color")
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

void PointCloudPass::ensureTargets(uint32_t viewCount) {
    const VkDeviceSize totalPixels =
        VkDeviceSize(pixelsPerView_) * std::max(viewCount, 1u);
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
                             VkExtent2D extent, uint32_t viewCount) {
    active_ = false;
    dispatches_.clear();
    dispatchCopyBytes_ = 0;

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

    viewCount_ = std::max(viewCount, 1u);
    pixelsPerView_ = extent.width * extent.height;
    if (pixelsPerView_ == 0)
        return;

    const PointCloudSettings& settings = submission.pointCloudSettings;
    hqsActive_ = settings.hqs;
    ensureTargets(viewCount_);

    const int clipCount =
        static_cast<int>(std::min<size_t>(submission.clipPlanes.size(), SV_PC_CLIP_PLANES));
    const float hqsThreshold = std::max(settings.hqsThreshold, 0.0f);
    const int splatMaxRadius = std::max(settings.splatMaxRadius, 0);

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
    hostData.reserve(clouds.size() * viewCount_);

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

        for (uint32_t v = 0; v < viewCount_; ++v) {
            const ViewCamera& cam = submission.views[std::min(v, kMaxViews - 1)];
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
            d.framebuffer = fbBase + VkDeviceSize(v) * pixelsPerView_ * sizeof(uint64_t);
            d.colorbuffer = colorBase + VkDeviceSize(v) * pixelsPerView_ * sizeof(uint32_t);
            d.hqsDepth = hqsDepthBase + VkDeviceSize(v) * pixelsPerView_ * sizeof(uint32_t);
            d.hqsAccum = hqsAccumBase + VkDeviceSize(v) * pixelsPerView_ * 4 * sizeof(uint32_t);
            d.imageWidth = static_cast<int>(extent.width);
            d.imageHeight = static_cast<int>(extent.height);
            d.pointsPerThread = item.pointsPerThread;
            d.cloudID = cloudID;
            d.splatMaxRadius = splatMaxRadius;
            d.clipPlaneCount = clipCount;
            d.hqsThreshold = hqsThreshold;
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
            dispatchBase + ci * viewCount_ * sizeof(gpu::PointCloudDispatch);
        record.numBatches = clouds[ci]->numBatches;
        dispatches_.push_back(record);
    }

    lookupPushes_.clear();
    resolvePush_ = {};
    hqsResolvePush_ = {};
    if (hqsActive_) {
        hqsResolvePush_.hqsDepth = hqsDepthBase;
        hqsResolvePush_.hqsAccum = hqsAccumBase;
        for (uint32_t v = 0; v < kMaxViews; ++v) {
            const ViewCamera& cam = submission.views[std::min(v, kMaxViews - 1)];
            // Window depth of linear eye depth d: -proj[2][2] + proj[3][2]/d.
            hqsResolvePush_.projAB[v] =
                glm::vec4(cam.proj[2][2], cam.proj[3][2], 0.0f, 0.0f);
        }
        hqsResolvePush_.imageWidth = static_cast<int>(extent.width);
        hqsResolvePush_.imageHeight = static_cast<int>(extent.height);
        hqsResolvePush_.pixelsPerView = pixelsPerView_;
    } else {
        // One lookup dispatch per view; the shader walks the dispatch array
        // by cloudID (cloud-major layout: stride = viewCount structs).
        for (uint32_t v = 0; v < viewCount_; ++v) {
            gpu::PointCloudLookupPush lookup{};
            lookup.framebuffer =
                fbBase + VkDeviceSize(v) * pixelsPerView_ * sizeof(uint64_t);
            lookup.colorbuffer =
                colorBase + VkDeviceSize(v) * pixelsPerView_ * sizeof(uint32_t);
            lookup.dispatchBase = dispatchBase + v * sizeof(gpu::PointCloudDispatch);
            lookup.cloudStrideBytes =
                viewCount_ * static_cast<uint32_t>(sizeof(gpu::PointCloudDispatch));
            lookup.pixelCount = pixelsPerView_;
            lookupPushes_.push_back(lookup);
        }

        resolvePush_.framebuffer = fbBase;
        resolvePush_.colorbuffer = colorBase;
        resolvePush_.imageWidth = static_cast<int>(extent.width);
        resolvePush_.imageHeight = static_cast<int>(extent.height);
        resolvePush_.pixelsPerView = pixelsPerView_;
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
    // point once and projects it per eye), chunked against
    // maxComputeWorkGroupCount[0].
    auto dispatchGeometry = [&](const rhi::Pipeline& pipeline) {
        pipeline.bind(cmd);
        for (const DispatchRecord& record : dispatches_) {
            for (uint32_t base = 0; base < record.numBatches; base += maxGroupsX_) {
                gpu::PointCloudComputePush push{};
                push.dispatchData = record.dispatchData;
                push.baseBatch = base;
                push.viewCount = viewCount_;
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

    if (hqsActive_) {
        // HQS: every cloud's depth dispatch must finish before ANY colour
        // dispatch (the colour pass reads the global nearest depth).
        dispatchGeometry(hqsDepthPipeline_);
        computeToCompute();
        dispatchGeometry(hqsColorPipeline_);
    } else {
        dispatchGeometry(rasterizePipeline_);
        computeToCompute();

        // Colour lookup: ONE dispatch per view resolves every pixel against
        // its owning cloud (cloudID -> dispatch-array pointer walk in the
        // shader) — the GL app needed a fullscreen dispatch per cloud here.
        // 2D grid (shader flattens row-major) so the group count stays under
        // maxComputeWorkGroupCount[0] even for 8K-class targets.
        lookupPipeline_.bind(cmd);
        const uint32_t groups = (pixelsPerView_ + SV_PC_LOOKUP_WORKGROUP - 1) /
                                SV_PC_LOOKUP_WORKGROUP;
        const uint32_t groupsX = std::min(groups, maxGroupsX_);
        const uint32_t groupsY = (groups + groupsX - 1) / groupsX;
        for (const gpu::PointCloudLookupPush& push : lookupPushes_) {
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

void PointCloudPass::recordResolve(VkCommandBuffer cmd) const {
    if (!active_)
        return;
    beginLabel(cmd, "pointcloud resolve");
    if (hqsActive_) {
        hqsResolvePipeline_.bind(cmd);
        hqsResolvePipeline_.pushConstants(cmd, &hqsResolvePush_, sizeof(hqsResolvePush_));
    } else {
        resolvePipeline_.bind(cmd);
        resolvePipeline_.pushConstants(cmd, &resolvePush_, sizeof(resolvePush_));
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
    rasterizePipeline_.destroy();
    lookupPipeline_.destroy();
    hqsDepthPipeline_.destroy();
    hqsColorPipeline_.destroy();
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
