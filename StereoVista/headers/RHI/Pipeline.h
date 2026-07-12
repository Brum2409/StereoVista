#pragma once

#include "RHI/VulkanCommon.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rhi {

class Device;

// Descriptor-set and pipeline layouts are REFLECTED from the SPIR-V
// (SPIRV-Reflect), merged across stages, so shaders are the single source of
// truth for set/binding/type/push-constant layout — no hand-maintained C++
// mirrors to drift. Overrides exist only for what SPIR-V cannot express:
//
//   * runtime (bindless) arrays need a real capacity + binding flags
//     (PARTIALLY_BOUND / UPDATE_AFTER_BIND / VARIABLE_COUNT) — bindingOverride;
//   * pipelines that must share a set layout object created elsewhere
//     use externalSetLayout (that set is then not owned by this pipeline).
struct BindingOverride {
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t descriptorCount = 0; // capacity for runtime arrays
    VkDescriptorBindingFlags flags = 0;
};

// Compiled pipeline + its layouts. Move-only RAII; owns the VkPipeline, the
// pipeline layout, and every reflected set layout (external ones excluded).
class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline() { destroy(); }
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&& other) noexcept { moveFrom(other); }
    Pipeline& operator=(Pipeline&& other) noexcept {
        if (this != &other) {
            destroy();
            moveFrom(other);
        }
        return *this;
    }

    void destroy();
    bool valid() const { return pipeline_ != VK_NULL_HANDLE; }

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }
    VkPipelineBindPoint bindPoint() const { return bindPoint_; }
    uint32_t setLayoutCount() const { return static_cast<uint32_t>(setLayouts_.size()); }
    VkDescriptorSetLayout setLayout(uint32_t set) const { return setLayouts_[set]; }

    // Merged push-constant coverage (single range). Zero size = none.
    VkShaderStageFlags pushConstantStages() const { return pushStages_; }
    uint32_t pushConstantSize() const { return pushSize_; }

    void bind(VkCommandBuffer cmd) const { vkCmdBindPipeline(cmd, bindPoint_, pipeline_); }
    void pushConstants(VkCommandBuffer cmd, const void* data, uint32_t size,
                       uint32_t offset = 0) const {
        vkCmdPushConstants(cmd, layout_, pushStages_, offset, size, data);
    }

private:
    friend class GraphicsPipelineBuilder;
    friend class ComputePipelineBuilder;
    void moveFrom(Pipeline& other);

    Device* device_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipelineBindPoint bindPoint_ = VK_PIPELINE_BIND_POINT_GRAPHICS;
    std::vector<VkDescriptorSetLayout> setLayouts_;
    std::vector<bool> ownsSetLayout_;
    VkShaderStageFlags pushStages_ = 0;
    uint32_t pushSize_ = 0;
};

struct VertexAttribute {
    uint32_t location = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t offset = 0;
};

struct VertexBinding {
    uint32_t binding = 0;
    uint32_t stride = 0;
    VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VertexAttribute> attributes;
};

enum class BlendMode { Opaque, AlphaBlend, Additive };

// Dynamic-rendering graphics pipeline. Viewport/scissor are always dynamic
// state. Defaults follow the house conventions (Renderer/Projection.h):
// COUNTER_CLOCKWISE front face, reverse-Z GREATER depth test.
class GraphicsPipelineBuilder {
public:
    // fragmentSpirv may be empty for depth-only pipelines (shadow casters):
    // no fragment stage, no color attachments — the early-Z fast path.
    GraphicsPipelineBuilder& setShaders(std::vector<uint32_t> vertexSpirv,
                                        std::vector<uint32_t> fragmentSpirv = {});
    GraphicsPipelineBuilder& setColorFormats(std::vector<VkFormat> formats);
    GraphicsPipelineBuilder& setDepthFormat(VkFormat format);
    GraphicsPipelineBuilder& setViewMask(uint32_t mask);
    GraphicsPipelineBuilder& setTopology(VkPrimitiveTopology topology);
    GraphicsPipelineBuilder& setPolygonMode(VkPolygonMode mode);
    GraphicsPipelineBuilder& setCullMode(VkCullModeFlags mode);
    GraphicsPipelineBuilder& setFrontFace(VkFrontFace face);
    GraphicsPipelineBuilder& setDepth(bool test, bool write,
                                      VkCompareOp op = VK_COMPARE_OP_GREATER);
    // Static rasterizer depth bias for shadow casters. NOTE reverse-Z: to
    // push casters AWAY from the light the offset must be NEGATIVE (depth
    // decreases with distance).
    GraphicsPipelineBuilder& setDepthBias(float constantFactor, float slopeFactor);
    GraphicsPipelineBuilder& setBlend(BlendMode mode);
    GraphicsPipelineBuilder& addVertexBinding(VertexBinding binding);
    // Widen dynamic state beyond the always-dynamic viewport/scissor
    // (playbook A.6) — e.g. depth test/compare/write + cull mode for the
    // overlay pipeline, all core 1.3. The static value set on this builder
    // becomes irrelevant for that state; the recorder must set it.
    GraphicsPipelineBuilder& addDynamicState(VkDynamicState state);
    GraphicsPipelineBuilder& bindingOverride(const BindingOverride& override_);
    GraphicsPipelineBuilder& externalSetLayout(uint32_t set, VkDescriptorSetLayout layout);
    GraphicsPipelineBuilder& setDebugName(std::string name);

    Pipeline build(Device& device);

private:
    std::vector<uint32_t> vertexSpirv_;
    std::vector<uint32_t> fragmentSpirv_;
    std::vector<VkFormat> colorFormats_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    uint32_t viewMask_ = 0;
    VkPrimitiveTopology topology_ = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPolygonMode polygonMode_ = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode_ = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace_ = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool depthTest_ = false;
    bool depthWrite_ = false;
    VkCompareOp depthOp_ = VK_COMPARE_OP_GREATER;
    bool depthBiasEnable_ = false;
    float depthBiasConstant_ = 0.0f;
    float depthBiasSlope_ = 0.0f;
    BlendMode blend_ = BlendMode::Opaque;
    std::vector<VertexBinding> vertexBindings_;
    std::vector<VkDynamicState> extraDynamicStates_;
    std::vector<BindingOverride> overrides_;
    std::vector<std::pair<uint32_t, VkDescriptorSetLayout>> externalLayouts_;
    std::string debugName_ = "graphics pipeline";
};

class ComputePipelineBuilder {
public:
    ComputePipelineBuilder& setShader(std::vector<uint32_t> computeSpirv);
    // Specialization constant (32-bit) baked at pipeline creation — the
    // driver finalizes codegen per value, so e.g. a spec-constant-sized array
    // costs exactly its specialized size in registers/scratch. Used to build
    // small/large variants of one kernel (point-cloud view cap).
    ComputePipelineBuilder& setSpecConstant(uint32_t constantId, uint32_t value);
    ComputePipelineBuilder& bindingOverride(const BindingOverride& override_);
    ComputePipelineBuilder& externalSetLayout(uint32_t set, VkDescriptorSetLayout layout);
    ComputePipelineBuilder& setDebugName(std::string name);

    Pipeline build(Device& device);

private:
    std::vector<uint32_t> computeSpirv_;
    std::vector<std::pair<uint32_t, uint32_t>> specConstants_; // (id, value)
    std::vector<BindingOverride> overrides_;
    std::vector<std::pair<uint32_t, VkDescriptorSetLayout>> externalLayouts_;
    std::string debugName_ = "compute pipeline";
};

} // namespace rhi
