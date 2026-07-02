#include "Renderer/Renderer.h"

#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"
#include "RHI/Swapchain.h"
#include "Renderer/Projection.h"

#include <glm/gtc/matrix_transform.hpp>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_vulkan.h"

#include <cmath>
#include <cstring>

namespace renderer {

namespace {

VkImageMemoryBarrier2 imageBarrier(VkImage image, VkImageAspectFlags aspect,
                                   VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                                   VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    return barrier;
}

void pipelineBarriers(VkCommandBuffer cmd, const VkImageMemoryBarrier2* barriers,
                      uint32_t count) {
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = count;
    dep.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace

void Renderer::init(rhi::Device& device, rhi::Swapchain& swapchain,
                    rhi::ShaderCompiler& shaderCompiler) {
    device_ = &device;
    swapchain_ = &swapchain;

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
    createTrianglePipeline(shaderCompiler);
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

        // Per-frame descriptor pool, wholesale-reset at slot reuse; sized for
        // the passes this phase records (grows into a real allocator in
        // Phase 2).
        VkDescriptorPoolSize sizes[1];
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[0].descriptorCount = 8;
        VkDescriptorPoolCreateInfo dpInfo{};
        dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpInfo.maxSets = 8;
        dpInfo.poolSizeCount = 1;
        dpInfo.pPoolSizes = sizes;
        VK_CHECK(vkCreateDescriptorPool(device_->device(), &dpInfo, nullptr, &frame.descriptorPool));

        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = sizeof(ViewUniforms);
        bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo bufAlloc{};
        bufAlloc.usage = VMA_MEMORY_USAGE_AUTO;
        bufAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo mapped{};
        VK_CHECK(vmaCreateBuffer(device_->allocator(), &bufInfo, &bufAlloc, &frame.viewUbo,
                                 &frame.viewUboAllocation, &mapped));
        frame.viewUboMapped = mapped.pMappedData;
    }
}

void Renderer::createSceneTarget() {
    destroySceneTarget();
    sceneExtent_ = swapchain_->extent();

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = kSceneColorFormat;
    imgInfo.extent.width = sceneExtent_.width;
    imgInfo.extent.height = sceneExtent_.height;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = viewCount_;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo gpuOnly{};
    gpuOnly.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    gpuOnly.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VK_CHECK(vmaCreateImage(device_->allocator(), &imgInfo, &gpuOnly, &sceneColor_,
                            &sceneColorAllocation_, nullptr));

    imgInfo.format = kSceneDepthFormat;
    imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VK_CHECK(vmaCreateImage(device_->allocator(), &imgInfo, &gpuOnly, &sceneDepth_,
                            &sceneDepthAllocation_, nullptr));

    // 2D_ARRAY views even at 1 layer: multiview attachments address layers by
    // view index, and mono must not be a special case.
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = viewCount_;

    viewInfo.image = sceneColor_;
    viewInfo.format = kSceneColorFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VK_CHECK(vkCreateImageView(device_->device(), &viewInfo, nullptr, &sceneColorView_));

    viewInfo.image = sceneDepth_;
    viewInfo.format = kSceneDepthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    VK_CHECK(vkCreateImageView(device_->device(), &viewInfo, nullptr, &sceneDepthView_));
}

void Renderer::createTrianglePipeline(rhi::ShaderCompiler& shaderCompiler) {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = 1;
    setInfo.pBindings = &uboBinding;
    VK_CHECK(vkCreateDescriptorSetLayout(device_->device(), &setInfo, nullptr, &viewSetLayout_));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &viewSetLayout_;
    VK_CHECK(vkCreatePipelineLayout(device_->device(), &layoutInfo, nullptr,
                                    &trianglePipelineLayout_));

    VkShaderModule vert = shaderCompiler.loadModule(device_->device(),
                                                    "assets/shaders_vk/triangle.vert");
    VkShaderModule frag = shaderCompiler.loadModule(device_->device(),
                                                    "assets/shaders_vk/triangle.frag");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    // House conventions (Renderer/Projection.h): Y-flipped projections turn
    // CCW-authored faces clockwise.
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Reverse-Z: clear 0.0, pass what is nearer (greater).
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    // Dynamic rendering + multiview: all views of the layered target in one
    // pass, gl_ViewIndex selects the camera.
    const uint32_t viewMask = (1u << viewCount_) - 1u;
    VkFormat colorFormat = kSceneColorFormat;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.viewMask = viewMask;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = kSceneDepthFormat;

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.pNext = &rendering;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &inputAssembly;
    pipeInfo.pViewportState = &viewport;
    pipeInfo.pRasterizationState = &raster;
    pipeInfo.pMultisampleState = &multisample;
    pipeInfo.pDepthStencilState = &depth;
    pipeInfo.pColorBlendState = &blend;
    pipeInfo.pDynamicState = &dynamic;
    pipeInfo.layout = trianglePipelineLayout_;

    VK_CHECK(vkCreateGraphicsPipelines(device_->device(), VK_NULL_HANDLE, 1, &pipeInfo,
                                       nullptr, &trianglePipeline_));

    vkDestroyShaderModule(device_->device(), vert, nullptr);
    vkDestroyShaderModule(device_->device(), frag, nullptr);
}

void Renderer::updateViewUniforms(FrameContext& frame, double timeSeconds) {
    // Gentle ±60° sway: proves the per-frame UBO path while keeping the
    // back-face-culled triangle facing the camera — if it vanishes, the
    // CLOCKWISE front-face convention (Projection.h) is broken.
    const float angle = std::sin(static_cast<float>(timeSeconds) * 0.5f) * glm::radians(60.0f);
    const glm::vec3 eye(3.0f * std::sin(angle), 1.2f, 3.0f * std::cos(angle));
    const glm::vec3 target(0.0f);
    const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const float aspect = sceneExtent_.height
                             ? static_cast<float>(sceneExtent_.width) / sceneExtent_.height
                             : 1.0f;
    const glm::mat4 proj = renderer::perspective(glm::radians(60.0f), aspect, 0.05f, 100.0f);

    ViewUniforms uniforms{};
    for (uint32_t v = 0; v < kMaxViews; ++v) {
        // Identical eyes until stereo (Phase 7) supplies per-eye transforms.
        uniforms.viewProj[v] = proj * view;
        uniforms.cameraPos[v] = glm::vec4(eye, 1.0f);
    }
    std::memcpy(frame.viewUboMapped, &uniforms, sizeof(uniforms));
    vmaFlushAllocation(device_->allocator(), frame.viewUboAllocation, 0, sizeof(uniforms));
}

bool Renderer::renderFrame(ImDrawData* uiDrawData, double timeSeconds) {
    FrameContext& frame = frames_[frameSlot_];

    // Block until this slot's previous submission retired.
    if (frame.submittedTimelineValue != 0) {
        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &frameTimeline_;
        waitInfo.pValues = &frame.submittedTimelineValue;
        VK_CHECK(vkWaitSemaphores(device_->device(), &waitInfo, UINT64_MAX));
    }

    const uint32_t imageIndex = swapchain_->acquireImage(frame.acquireSemaphore);
    if (imageIndex == UINT32_MAX)
        return false;

    VK_CHECK(vkResetCommandPool(device_->device(), frame.commandPool, 0));
    VK_CHECK(vkResetDescriptorPool(device_->device(), frame.descriptorPool, 0));
    updateViewUniforms(frame, timeSeconds);
    recordFrame(frame, imageIndex, uiDrawData);

    VkSemaphoreSubmitInfo waitAcquire{};
    waitAcquire.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitAcquire.semaphore = frame.acquireSemaphore;
    // First swapchain-image access this frame is the blit-dst transition.
    waitAcquire.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;

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

    frameSlot_ = (frameSlot_ + 1) % kFramesInFlight;

    return swapchain_->present(device_->graphicsQueue(), imageIndex);
}

void Renderer::recordFrame(FrameContext& frame, uint32_t imageIndex, ImDrawData* uiDrawData) {
    VkCommandBuffer cmd = frame.commandBuffer;
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // Scene target: previous contents are irrelevant (UNDEFINED discard).
    {
        VkImageMemoryBarrier2 barriers[2] = {
            imageBarrier(sceneColor_, VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
            imageBarrier(sceneDepth_, VK_IMAGE_ASPECT_DEPTH_BIT,
                         VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL),
        };
        pipelineBarriers(cmd, barriers, 2);
    }

    // ---- Scene pass (multiview, all layers in one go) ----
    {
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = sceneColorView_;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color.float32[0] = 0.045f;
        color.clearValue.color.float32[1] = 0.05f;
        color.clearValue.color.float32[2] = 0.07f;
        color.clearValue.color.float32[3] = 1.0f;

        VkRenderingAttachmentInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = sceneDepthView_;
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

        VkDescriptorSetAllocateInfo setAlloc{};
        setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setAlloc.descriptorPool = frame.descriptorPool;
        setAlloc.descriptorSetCount = 1;
        setAlloc.pSetLayouts = &viewSetLayout_;
        VkDescriptorSet viewSet = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateDescriptorSets(device_->device(), &setAlloc, &viewSet));

        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = frame.viewUbo;
        uboInfo.range = sizeof(ViewUniforms);
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = viewSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &uboInfo;
        vkUpdateDescriptorSets(device_->device(), 1, &write, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipelineLayout_,
                                0, 1, &viewSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);
    }

    // ---- Resolve to swapchain: blit view 0 (mono) ----
    VkImage backbuffer = swapchain_->image(imageIndex);
    {
        VkImageMemoryBarrier2 barriers[2] = {
            imageBarrier(sceneColor_, VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
            imageBarrier(backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                         VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
        };
        pipelineBarriers(cmd, barriers, 2);

        VkImageBlit2 region{};
        region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.layerCount = 1;
        region.srcOffsets[1].x = static_cast<int32_t>(sceneExtent_.width);
        region.srcOffsets[1].y = static_cast<int32_t>(sceneExtent_.height);
        region.srcOffsets[1].z = 1;
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.layerCount = 1;
        region.dstOffsets[1].x = static_cast<int32_t>(swapchain_->extent().width);
        region.dstOffsets[1].y = static_cast<int32_t>(swapchain_->extent().height);
        region.dstOffsets[1].z = 1;

        VkBlitImageInfo2 blit{};
        blit.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blit.srcImage = sceneColor_;
        blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blit.dstImage = backbuffer;
        blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blit.regionCount = 1;
        blit.pRegions = &region;
        blit.filter = VK_FILTER_LINEAR;
        vkCmdBlitImage2(cmd, &blit);
    }

    // ---- UI pass straight onto the backbuffer ----
    {
        VkImageMemoryBarrier2 toColor = imageBarrier(
            backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        pipelineBarriers(cmd, &toColor, 1);

        if (uiDrawData) {
            VkRenderingAttachmentInfo color{};
            color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color.imageView = swapchain_->imageView(imageIndex);
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
            ImGui_ImplVulkan_RenderDrawData(uiDrawData, cmd);
            vkCmdEndRendering(cmd);
        }

        VkImageMemoryBarrier2 toPresent = imageBarrier(
            backbuffer, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, 0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        pipelineBarriers(cmd, &toPresent, 1);
    }

    VK_CHECK(vkEndCommandBuffer(cmd));
}

void Renderer::onSwapchainRecreated() {
    createSceneTarget();
}

void Renderer::destroySceneTarget() {
    if (!device_)
        return;
    if (sceneColorView_ != VK_NULL_HANDLE)
        vkDestroyImageView(device_->device(), sceneColorView_, nullptr);
    if (sceneColor_ != VK_NULL_HANDLE)
        vmaDestroyImage(device_->allocator(), sceneColor_, sceneColorAllocation_);
    if (sceneDepthView_ != VK_NULL_HANDLE)
        vkDestroyImageView(device_->device(), sceneDepthView_, nullptr);
    if (sceneDepth_ != VK_NULL_HANDLE)
        vmaDestroyImage(device_->allocator(), sceneDepth_, sceneDepthAllocation_);
    sceneColorView_ = VK_NULL_HANDLE;
    sceneColor_ = VK_NULL_HANDLE;
    sceneColorAllocation_ = VK_NULL_HANDLE;
    sceneDepthView_ = VK_NULL_HANDLE;
    sceneDepth_ = VK_NULL_HANDLE;
    sceneDepthAllocation_ = VK_NULL_HANDLE;
}

void Renderer::destroyFrameContexts() {
    for (FrameContext& frame : frames_) {
        if (frame.viewUbo != VK_NULL_HANDLE)
            vmaDestroyBuffer(device_->allocator(), frame.viewUbo, frame.viewUboAllocation);
        if (frame.descriptorPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_->device(), frame.descriptorPool, nullptr);
        if (frame.acquireSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(device_->device(), frame.acquireSemaphore, nullptr);
        if (frame.commandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_->device(), frame.commandPool, nullptr);
        frame = FrameContext{};
    }
}

void Renderer::shutdown() {
    if (!device_)
        return;
    device_->waitIdle();
    if (trianglePipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device_->device(), trianglePipeline_, nullptr);
    if (trianglePipelineLayout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device_->device(), trianglePipelineLayout_, nullptr);
    if (viewSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device_->device(), viewSetLayout_, nullptr);
    destroySceneTarget();
    destroyFrameContexts();
    if (frameTimeline_ != VK_NULL_HANDLE)
        vkDestroySemaphore(device_->device(), frameTimeline_, nullptr);
    trianglePipeline_ = VK_NULL_HANDLE;
    trianglePipelineLayout_ = VK_NULL_HANDLE;
    viewSetLayout_ = VK_NULL_HANDLE;
    frameTimeline_ = VK_NULL_HANDLE;
    device_ = nullptr;
    swapchain_ = nullptr;
}

} // namespace renderer
