#pragma once

#include "RHI/Buffer.h"
#include "RHI/Pipeline.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace rhi {
class Device;
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

    VkBuffer vertexBuffer() const { return vertices_.handle(); }
    VkBuffer indexBuffer() const { return indices_.handle(); }
    uint32_t indexCount() const { return indexCount_; }

    void bindAndDraw(VkCommandBuffer cmd) const;

    // Full vertex layout for the forward pass.
    static rhi::VertexBinding vertexBinding();
    // Position-only layout for the depth-only shadow pipelines (same stride,
    // unused attributes simply not declared).
    static rhi::VertexBinding positionOnlyBinding();

private:
    rhi::Buffer vertices_;
    rhi::Buffer indices_;
    uint32_t indexCount_ = 0;
};

} // namespace renderer
