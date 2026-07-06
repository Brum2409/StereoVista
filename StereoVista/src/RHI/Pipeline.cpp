#include "RHI/Pipeline.h"

#include "RHI/Device.h"

#include <spirv_reflect/spirv_reflect.h>

#include <algorithm>
#include <map>
#include <stdexcept>

namespace rhi {

namespace {

// One shader stage's reflected interface.
struct StageReflection {
    VkShaderStageFlagBits stage{};
    std::string entryPoint;
    struct Binding {
        uint32_t set = 0;
        uint32_t binding = 0;
        VkDescriptorType type{};
        uint32_t count = 0; // 0 = runtime array (needs a BindingOverride)
    };
    std::vector<Binding> bindings;
    uint32_t pushOffset = 0;
    uint32_t pushSize = 0; // 0 = no push constants
};

StageReflection reflectStage(const std::vector<uint32_t>& spirv,
                             const std::string& pipelineName) {
    SpvReflectShaderModule module{};
    if (spvReflectCreateShaderModule(spirv.size() * sizeof(uint32_t), spirv.data(),
                                     &module) != SPV_REFLECT_RESULT_SUCCESS)
        throw std::runtime_error("Pipeline '" + pipelineName +
                                 "': SPIRV-Reflect failed to parse a shader");

    StageReflection out;
    out.stage = static_cast<VkShaderStageFlagBits>(module.shader_stage);
    out.entryPoint = module.entry_point_name ? module.entry_point_name : "main";

    uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    std::vector<SpvReflectDescriptorBinding*> bindings(count);
    spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());
    for (const SpvReflectDescriptorBinding* b : bindings) {
        StageReflection::Binding binding;
        binding.set = b->set;
        binding.binding = b->binding;
        binding.type = static_cast<VkDescriptorType>(b->descriptor_type);
        binding.count = b->count; // SPIRV-Reflect reports 0 for runtime arrays
        out.bindings.push_back(binding);
    }

    count = 0;
    spvReflectEnumeratePushConstantBlocks(&module, &count, nullptr);
    std::vector<SpvReflectBlockVariable*> blocks(count);
    spvReflectEnumeratePushConstantBlocks(&module, &count, blocks.data());
    for (const SpvReflectBlockVariable* block : blocks) {
        const uint32_t begin = block->offset;
        const uint32_t end = block->offset + block->size;
        if (out.pushSize == 0) {
            out.pushOffset = begin;
            out.pushSize = end - begin;
        } else {
            const uint32_t mergedBegin = std::min(out.pushOffset, begin);
            const uint32_t mergedEnd = std::max(out.pushOffset + out.pushSize, end);
            out.pushOffset = mergedBegin;
            out.pushSize = mergedEnd - mergedBegin;
        }
    }

    spvReflectDestroyShaderModule(&module);
    return out;
}

struct MergedBinding {
    VkDescriptorType type{};
    uint32_t count = 0;
    VkShaderStageFlags stages = 0;
    VkDescriptorBindingFlags flags = 0;
};

// set -> binding -> merged info. std::map keeps sets/bindings ordered so
// layout creation is deterministic.
using MergedLayout = std::map<uint32_t, std::map<uint32_t, MergedBinding>>;

// Merge per-stage reflections: same (set,binding) must agree on type and
// count across stages; stage flags accumulate.
MergedLayout mergeStages(const std::vector<StageReflection>& stages,
                         const std::string& pipelineName) {
    MergedLayout merged;
    for (const StageReflection& stage : stages) {
        for (const StageReflection::Binding& b : stage.bindings) {
            MergedBinding& slot = merged[b.set][b.binding];
            if (slot.stages == 0) {
                slot.type = b.type;
                slot.count = b.count;
            } else if (slot.type != b.type || slot.count != b.count) {
                throw std::runtime_error(
                    "Pipeline '" + pipelineName + "': set " + std::to_string(b.set) +
                    " binding " + std::to_string(b.binding) +
                    " is declared differently across shader stages");
            }
            slot.stages |= stage.stage;
        }
    }
    return merged;
}

void applyOverrides(MergedLayout& merged, const std::vector<BindingOverride>& overrides,
                    const std::vector<std::pair<uint32_t, VkDescriptorSetLayout>>& external,
                    const std::string& pipelineName) {
    for (const BindingOverride& o : overrides) {
        auto setIt = merged.find(o.set);
        if (setIt == merged.end() || setIt->second.find(o.binding) == setIt->second.end())
            throw std::runtime_error("Pipeline '" + pipelineName + "': override targets set " +
                                     std::to_string(o.set) + " binding " +
                                     std::to_string(o.binding) +
                                     " which no shader stage declares");
        MergedBinding& slot = setIt->second[o.binding];
        if (o.descriptorCount > 0)
            slot.count = o.descriptorCount;
        slot.flags = o.flags;
    }
    // Runtime arrays need a capacity — but only in sets THIS pipeline builds.
    // An externalSetLayout set (e.g. the MaterialSystem's bindless array) is
    // authoritative as-is; its reflected bindings are discarded by
    // buildLayouts, so demanding a duplicate capacity here would only invite
    // drift from the real layout.
    for (const auto& [setIndex, bindings] : merged) {
        const bool isExternal =
            std::find_if(external.begin(), external.end(), [&, s = setIndex](const auto& e) {
                return e.first == s;
            }) != external.end();
        if (isExternal)
            continue;
        for (const auto& [bindingIndex, slot] : bindings)
            if (slot.count == 0)
                throw std::runtime_error(
                    "Pipeline '" + pipelineName + "': set " + std::to_string(setIndex) +
                    " binding " + std::to_string(bindingIndex) +
                    " is a runtime array; give it a capacity via bindingOverride()"
                    " (or provide the whole set via externalSetLayout)");
    }
}

// Everything buildLayouts produces; the builders (friends of Pipeline) move
// these into the pipeline's private members.
struct BuiltLayouts {
    std::vector<VkDescriptorSetLayout> setLayouts;
    std::vector<bool> owns;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
};

// Builds the descriptor set layouts (respecting external substitutions) and
// the pipeline layout.
BuiltLayouts buildLayouts(Device& device, const MergedLayout& merged,
                          const std::vector<std::pair<uint32_t, VkDescriptorSetLayout>>& external,
                          VkShaderStageFlags pushStages, uint32_t pushOffset,
                          uint32_t pushSize) {
    uint32_t setCount = 0;
    for (const auto& [setIndex, bindings] : merged)
        setCount = std::max(setCount, setIndex + 1);
    for (const auto& [setIndex, layout] : external)
        setCount = std::max(setCount, setIndex + 1);

    BuiltLayouts built;
    built.setLayouts.assign(setCount, VK_NULL_HANDLE);
    built.owns.assign(setCount, false);

    for (uint32_t setIndex = 0; setIndex < setCount; ++setIndex) {
        auto externalIt = std::find_if(external.begin(), external.end(),
                                       [&](const auto& e) { return e.first == setIndex; });
        if (externalIt != external.end()) {
            built.setLayouts[setIndex] = externalIt->second;
            continue;
        }

        std::vector<VkDescriptorSetLayoutBinding> bindings;
        std::vector<VkDescriptorBindingFlags> bindingFlags;
        bool anyFlags = false;
        bool updateAfterBind = false;
        auto setIt = merged.find(setIndex);
        if (setIt != merged.end()) {
            for (const auto& [bindingIndex, slot] : setIt->second) {
                VkDescriptorSetLayoutBinding b{};
                b.binding = bindingIndex;
                b.descriptorType = slot.type;
                b.descriptorCount = slot.count;
                b.stageFlags = slot.stages;
                bindings.push_back(b);
                bindingFlags.push_back(slot.flags);
                anyFlags |= slot.flags != 0;
                updateAfterBind |=
                    (slot.flags & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT) != 0;
            }
        }

        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        flagsInfo.pBindingFlags = bindingFlags.data();

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        if (anyFlags)
            layoutInfo.pNext = &flagsInfo;
        if (updateAfterBind)
            layoutInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

        VK_CHECK(vkCreateDescriptorSetLayout(device.device(), &layoutInfo, nullptr,
                                             &built.setLayouts[setIndex]));
        built.owns[setIndex] = true;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = pushStages;
    pushRange.offset = pushOffset;
    pushRange.size = pushSize;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = setCount;
    layoutInfo.pSetLayouts = built.setLayouts.data();
    if (pushSize > 0) {
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
    }
    VK_CHECK(vkCreatePipelineLayout(device.device(), &layoutInfo, nullptr,
                                    &built.pipelineLayout));
    return built;
}

VkShaderModule createModule(Device& device, const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spirv.size() * sizeof(uint32_t);
    info.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device.device(), &info, nullptr, &module));
    return module;
}

// Push-constant coverage merged across stages into ONE range: union of byte
// extents, union of stage flags. Always valid (a range may cover more bytes
// than a stage declares) and lets callers push once for all stages.
void mergePush(const std::vector<StageReflection>& stages, VkShaderStageFlags& outStages,
               uint32_t& outOffset, uint32_t& outSize) {
    outStages = 0;
    uint32_t begin = UINT32_MAX, end = 0;
    for (const StageReflection& stage : stages) {
        if (stage.pushSize == 0)
            continue;
        outStages |= stage.stage;
        begin = std::min(begin, stage.pushOffset);
        end = std::max(end, stage.pushOffset + stage.pushSize);
    }
    outOffset = (end > 0) ? begin : 0;
    outSize = (end > 0) ? end - begin : 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Pipeline

void Pipeline::destroy() {
    if (device_) {
        if (pipeline_ != VK_NULL_HANDLE)
            vkDestroyPipeline(device_->device(), pipeline_, nullptr);
        if (layout_ != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device_->device(), layout_, nullptr);
        for (size_t i = 0; i < setLayouts_.size(); ++i)
            if (ownsSetLayout_[i] && setLayouts_[i] != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(device_->device(), setLayouts_[i], nullptr);
    }
    device_ = nullptr;
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    setLayouts_.clear();
    ownsSetLayout_.clear();
    pushStages_ = 0;
    pushSize_ = 0;
}

void Pipeline::moveFrom(Pipeline& other) {
    device_ = other.device_;
    pipeline_ = other.pipeline_;
    layout_ = other.layout_;
    bindPoint_ = other.bindPoint_;
    setLayouts_ = std::move(other.setLayouts_);
    ownsSetLayout_ = std::move(other.ownsSetLayout_);
    pushStages_ = other.pushStages_;
    pushSize_ = other.pushSize_;
    other.device_ = nullptr;
    other.pipeline_ = VK_NULL_HANDLE;
    other.layout_ = VK_NULL_HANDLE;
    other.setLayouts_.clear();
    other.ownsSetLayout_.clear();
    other.pushStages_ = 0;
    other.pushSize_ = 0;
}

// ---------------------------------------------------------------------------
// GraphicsPipelineBuilder

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setShaders(
    std::vector<uint32_t> vertexSpirv, std::vector<uint32_t> fragmentSpirv) {
    vertexSpirv_ = std::move(vertexSpirv);
    fragmentSpirv_ = std::move(fragmentSpirv);
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setColorFormats(std::vector<VkFormat> formats) {
    colorFormats_ = std::move(formats);
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setDepthFormat(VkFormat format) {
    depthFormat_ = format;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setViewMask(uint32_t mask) {
    viewMask_ = mask;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setTopology(VkPrimitiveTopology topology) {
    topology_ = topology;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setPolygonMode(VkPolygonMode mode) {
    polygonMode_ = mode;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setCullMode(VkCullModeFlags mode) {
    cullMode_ = mode;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setFrontFace(VkFrontFace face) {
    frontFace_ = face;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setDepth(bool test, bool write,
                                                           VkCompareOp op) {
    depthTest_ = test;
    depthWrite_ = write;
    depthOp_ = op;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setDepthBias(float constantFactor,
                                                               float slopeFactor) {
    depthBiasEnable_ = true;
    depthBiasConstant_ = constantFactor;
    depthBiasSlope_ = slopeFactor;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setBlend(BlendMode mode) {
    blend_ = mode;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::addVertexBinding(VertexBinding binding) {
    vertexBindings_.push_back(std::move(binding));
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::bindingOverride(const BindingOverride& override_) {
    overrides_.push_back(override_);
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::externalSetLayout(uint32_t set,
                                                                    VkDescriptorSetLayout layout) {
    externalLayouts_.emplace_back(set, layout);
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::addDynamicState(VkDynamicState state) {
    extraDynamicStates_.push_back(state);
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::setDebugName(std::string name) {
    debugName_ = std::move(name);
    return *this;
}

Pipeline GraphicsPipelineBuilder::build(Device& device) {
    if (vertexSpirv_.empty())
        throw std::runtime_error("Pipeline '" + debugName_ + "': missing vertex shader");
    const bool hasFragment = !fragmentSpirv_.empty();
    if (!hasFragment && !colorFormats_.empty())
        throw std::runtime_error("Pipeline '" + debugName_ +
                                 "': color attachments need a fragment shader");
    if (colorFormats_.empty() && depthFormat_ == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("Pipeline '" + debugName_ + "': no attachment formats");

    std::vector<StageReflection> stages;
    stages.push_back(reflectStage(vertexSpirv_, debugName_));
    if (hasFragment)
        stages.push_back(reflectStage(fragmentSpirv_, debugName_));
    if (stages[0].stage != VK_SHADER_STAGE_VERTEX_BIT ||
        (hasFragment && stages[1].stage != VK_SHADER_STAGE_FRAGMENT_BIT))
        throw std::runtime_error("Pipeline '" + debugName_ +
                                 "': shaders are not a vertex(+fragment) pair");

    MergedLayout merged = mergeStages(stages, debugName_);
    applyOverrides(merged, overrides_, externalLayouts_, debugName_);

    Pipeline pipeline;
    pipeline.device_ = &device;
    pipeline.bindPoint_ = VK_PIPELINE_BIND_POINT_GRAPHICS;

    VkShaderStageFlags pushStages = 0;
    uint32_t pushOffset = 0, pushSize = 0;
    mergePush(stages, pushStages, pushOffset, pushSize);
    BuiltLayouts built =
        buildLayouts(device, merged, externalLayouts_, pushStages, pushOffset, pushSize);
    pipeline.setLayouts_ = std::move(built.setLayouts);
    pipeline.ownsSetLayout_ = std::move(built.owns);
    pipeline.layout_ = built.pipelineLayout;
    pipeline.pushStages_ = pushSize > 0 ? pushStages : 0;
    pipeline.pushSize_ = pushSize;

    VkShaderModule vert = createModule(device, vertexSpirv_);
    VkShaderModule frag = hasFragment ? createModule(device, fragmentSpirv_)
                                      : VK_NULL_HANDLE;

    VkPipelineShaderStageCreateInfo stageInfos[2]{};
    uint32_t stageCount = 1;
    stageInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stageInfos[0].module = vert;
    stageInfos[0].pName = stages[0].entryPoint.c_str();
    if (hasFragment) {
        stageInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stageInfos[1].module = frag;
        stageInfos[1].pName = stages[1].entryPoint.c_str();
        stageCount = 2;
    }

    std::vector<VkVertexInputBindingDescription> vkBindings;
    std::vector<VkVertexInputAttributeDescription> vkAttributes;
    for (const VertexBinding& binding : vertexBindings_) {
        VkVertexInputBindingDescription b{};
        b.binding = binding.binding;
        b.stride = binding.stride;
        b.inputRate = binding.inputRate;
        vkBindings.push_back(b);
        for (const VertexAttribute& attr : binding.attributes) {
            VkVertexInputAttributeDescription a{};
            a.location = attr.location;
            a.binding = binding.binding;
            a.format = attr.format;
            a.offset = attr.offset;
            vkAttributes.push_back(a);
        }
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(vkBindings.size());
    vertexInput.pVertexBindingDescriptions = vkBindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vkAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vkAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topology_;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = polygonMode_;
    raster.cullMode = cullMode_;
    raster.frontFace = frontFace_;
    raster.depthBiasEnable = depthBiasEnable_ ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = depthBiasConstant_;
    raster.depthBiasSlopeFactor = depthBiasSlope_;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = depthTest_ ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = depthWrite_ ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = depthOp_;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    switch (blend_) {
    case BlendMode::Opaque:
        break;
    case BlendMode::AlphaBlend:
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case BlendMode::Additive:
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    }
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
        colorFormats_.size(), blendAttachment);
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    blend.pAttachments = blendAttachments.data();

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT,
                                                  VK_DYNAMIC_STATE_SCISSOR };
    dynamicStates.insert(dynamicStates.end(), extraDynamicStates_.begin(),
                         extraDynamicStates_.end());
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.viewMask = viewMask_;
    rendering.colorAttachmentCount = static_cast<uint32_t>(colorFormats_.size());
    rendering.pColorAttachmentFormats = colorFormats_.data();
    rendering.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.pNext = &rendering;
    pipeInfo.stageCount = stageCount;
    pipeInfo.pStages = stageInfos;
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &inputAssembly;
    pipeInfo.pViewportState = &viewport;
    pipeInfo.pRasterizationState = &raster;
    pipeInfo.pMultisampleState = &multisample;
    pipeInfo.pDepthStencilState = &depth;
    pipeInfo.pColorBlendState = &blend;
    pipeInfo.pDynamicState = &dynamic;
    pipeInfo.layout = pipeline.layout_;

    VK_CHECK(vkCreateGraphicsPipelines(device.device(), device.pipelineCache(), 1,
                                       &pipeInfo, nullptr, &pipeline.pipeline_));

    vkDestroyShaderModule(device.device(), vert, nullptr);
    if (frag != VK_NULL_HANDLE)
        vkDestroyShaderModule(device.device(), frag, nullptr);

    device.setDebugName(VK_OBJECT_TYPE_PIPELINE,
                        reinterpret_cast<uint64_t>(pipeline.pipeline_),
                        debugName_.c_str());
    return pipeline;
}

// ---------------------------------------------------------------------------
// ComputePipelineBuilder

ComputePipelineBuilder& ComputePipelineBuilder::setShader(std::vector<uint32_t> computeSpirv) {
    computeSpirv_ = std::move(computeSpirv);
    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::bindingOverride(const BindingOverride& override_) {
    overrides_.push_back(override_);
    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::externalSetLayout(uint32_t set,
                                                                  VkDescriptorSetLayout layout) {
    externalLayouts_.emplace_back(set, layout);
    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::setDebugName(std::string name) {
    debugName_ = std::move(name);
    return *this;
}

Pipeline ComputePipelineBuilder::build(Device& device) {
    if (computeSpirv_.empty())
        throw std::runtime_error("Pipeline '" + debugName_ + "': missing compute shader");

    std::vector<StageReflection> stages;
    stages.push_back(reflectStage(computeSpirv_, debugName_));
    if (stages[0].stage != VK_SHADER_STAGE_COMPUTE_BIT)
        throw std::runtime_error("Pipeline '" + debugName_ + "': shader is not compute");

    MergedLayout merged = mergeStages(stages, debugName_);
    applyOverrides(merged, overrides_, externalLayouts_, debugName_);

    Pipeline pipeline;
    pipeline.device_ = &device;
    pipeline.bindPoint_ = VK_PIPELINE_BIND_POINT_COMPUTE;

    VkShaderStageFlags pushStages = 0;
    uint32_t pushOffset = 0, pushSize = 0;
    mergePush(stages, pushStages, pushOffset, pushSize);
    BuiltLayouts built =
        buildLayouts(device, merged, externalLayouts_, pushStages, pushOffset, pushSize);
    pipeline.setLayouts_ = std::move(built.setLayouts);
    pipeline.ownsSetLayout_ = std::move(built.owns);
    pipeline.layout_ = built.pipelineLayout;
    pipeline.pushStages_ = pushSize > 0 ? pushStages : 0;
    pipeline.pushSize_ = pushSize;

    VkShaderModule module = createModule(device, computeSpirv_);

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.module = module;
    pipeInfo.stage.pName = stages[0].entryPoint.c_str();
    pipeInfo.layout = pipeline.layout_;

    VK_CHECK(vkCreateComputePipelines(device.device(), device.pipelineCache(), 1,
                                      &pipeInfo, nullptr, &pipeline.pipeline_));
    vkDestroyShaderModule(device.device(), module, nullptr);

    device.setDebugName(VK_OBJECT_TYPE_PIPELINE,
                        reinterpret_cast<uint64_t>(pipeline.pipeline_),
                        debugName_.c_str());
    return pipeline;
}

} // namespace rhi
