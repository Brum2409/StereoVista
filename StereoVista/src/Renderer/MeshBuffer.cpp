#include "Renderer/MeshBuffer.h"

#include "RHI/Device.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace renderer {

void MeshBuffer::create(rhi::Device& device, const MeshData& data,
                        const char* debugName) {
    if (data.vertices.empty() || data.indices.empty())
        throw std::runtime_error("MeshBuffer: empty mesh data for '" +
                                 std::string(debugName ? debugName : "?") + "'");
    destroy();

    rhi::BufferDesc vbDesc{};
    vbDesc.size = data.vertices.size() * sizeof(Vertex);
    vbDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbDesc.debugName = debugName;
    vertices_.create(device, vbDesc);
    vertices_.upload(data.vertices.data(), vbDesc.size);

    rhi::BufferDesc ibDesc{};
    ibDesc.size = data.indices.size() * sizeof(uint32_t);
    ibDesc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibDesc.debugName = debugName;
    indices_.create(device, ibDesc);
    indices_.upload(data.indices.data(), ibDesc.size);

    indexCount_ = static_cast<uint32_t>(data.indices.size());
}

void MeshBuffer::destroy() {
    vertices_.destroy();
    indices_.destroy();
    indexCount_ = 0;
}

void MeshBuffer::bindAndDraw(VkCommandBuffer cmd) const {
    VkBuffer vb = vertices_.handle();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(cmd, indices_.handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
}

rhi::VertexBinding MeshBuffer::vertexBinding() {
    rhi::VertexBinding binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.attributes = {
        { 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) },
        { 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) },
        { 2, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) },
        { 3, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent) },
    };
    return binding;
}

rhi::VertexBinding MeshBuffer::positionOnlyBinding() {
    rhi::VertexBinding binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.attributes = {
        { 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) },
    };
    return binding;
}

} // namespace renderer
