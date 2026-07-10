#pragma once

#include "RHI/Buffer.h"
#include "RHI/Pipeline.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace rhi {
class Device;
class UploadRing;
}

namespace renderer {

// CPU-side mesh interchange format. Producers: the Assimp importer and the
// procedural primitive factories (Scene/). Tangent packs the bitangent sign
// in w (bitangent = cross(N, T) * w in the vertex shader) — half the data of
// the GL layout's explicit bitangent stream.
struct Vertex {
    glm::vec3 position{ 0.0f };
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    glm::vec2 uv{ 0.0f };
    glm::vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
};
static_assert(sizeof(Vertex) == 48, "vertex layout is mirrored in vertexBinding()");

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// GPU mesh: staged-upload device-local vertex + index buffers (replaces the
// GL Mesh's VAO/VBO/EBO). Move-only RAII via the underlying rhi::Buffers.
class MeshBuffer {
public:
    MeshBuffer() = default;
    MeshBuffer(MeshBuffer&&) = default;
    MeshBuffer& operator=(MeshBuffer&&) = default;

    void create(rhi::Device& device, const MeshData& data,
                const char* debugName = nullptr);
    void destroy();
    bool valid() const { return indexCount_ > 0 && vertices_.valid(); }

    // Streaming path (SLPK plan §6.3): createForStreaming() makes the
    // device-local buffers UNFILLED (no blocking upload); stagePayload() then
    // copies the data through the UploadRing — all-or-nothing per call, false
    // = the ring is full right now, retry next frame (the buffers persist).
    // valid() flips true only when stagePayload succeeded; the caller must
    // not draw before the ring flushed into a frame (the flush precedes all
    // draws in the frame command buffer, so "staged this pump" is drawable
    // the same frame). dropPendingCopies() cancels staged-but-unflushed
    // copies before an early destroy (layer unload).
    void createForStreaming(rhi::Device& device, uint32_t vertexCount,
                            uint32_t indexCount, const char* debugName = nullptr);
    bool stagePayload(rhi::UploadRing& ring, const Vertex* vertices,
                      size_t vertexCount, const uint32_t* indices,
                      size_t indexCount);
    void dropPendingCopies(rhi::UploadRing& ring) const;

    VkBuffer vertexBuffer() const { return vertices_.handle(); }
    VkBuffer indexBuffer() const { return indices_.handle(); }
    uint32_t indexCount() const { return indexCount_; }

    void bindAndDraw(VkCommandBuffer cmd) const;

    // Full vertex layout for the forward pass.
    static rhi::VertexBinding vertexBinding();
    // Position-only layout for the depth-only shadow pipelines (same stride,
    // unused attributes simply not declared).
    static rhi::VertexBinding positionOnlyBinding();
    // Position + UV for the alpha-masked shadow pipelines (the fragment stage
    // samples albedo alpha).
    static rhi::VertexBinding positionUvBinding();

private:
    rhi::Buffer vertices_;
    rhi::Buffer indices_;
    uint32_t indexCount_ = 0;
};

} // namespace renderer
