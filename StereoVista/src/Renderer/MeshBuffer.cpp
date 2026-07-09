#include "Renderer/MeshBuffer.h"

#include "RHI/Device.h"
#include "RHI/UploadRing.h"

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

void MeshBuffer::createForStreaming(rhi::Device& device, uint32_t vertexCount,
                                    uint32_t indexCount, const char* debugName) {
    if (vertexCount == 0 || indexCount == 0)
        throw std::runtime_error("MeshBuffer: empty streaming mesh for '" +
                                 std::string(debugName ? debugName : "?") + "'");
    destroy();

    rhi::BufferDesc vbDesc{};
    vbDesc.size = VkDeviceSize(vertexCount) * sizeof(Vertex);
    vbDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; // GpuOnly adds TRANSFER_DST
    vbDesc.debugName = debugName;
    vertices_.create(device, vbDesc);

    rhi::BufferDesc ibDesc{};
    ibDesc.size = VkDeviceSize(indexCount) * sizeof(uint32_t);
    ibDesc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibDesc.debugName = debugName;
    indices_.create(device, ibDesc);
    // indexCount_ stays 0 (invalid) until stagePayload lands the data.
}

bool MeshBuffer::stagePayload(rhi::UploadRing& ring, const Vertex* vertices,
                              size_t vertexCount, const uint32_t* indices,
                              size_t indexCount) {
    if (!vertices_.valid() || !indices_.valid())
        throw std::runtime_error("MeshBuffer::stagePayload before createForStreaming");
    const VkDeviceSize vbBytes = vertexCount * sizeof(Vertex);
    const VkDeviceSize ibBytes = indexCount * sizeof(uint32_t);
    if (vbBytes != vertices_.size() || ibBytes != indices_.size())
        throw std::runtime_error("MeshBuffer::stagePayload size mismatch");

    const rhi::UploadRing::Marker marker = ring.mark();
    if (!ring.stage(vertices_, 0, vertices, vbBytes) ||
        !ring.stage(indices_, 0, indices, ibBytes)) {
        ring.rollback(marker);
        return false;
    }
    indexCount_ = static_cast<uint32_t>(indexCount);
    return true;
}

void MeshBuffer::dropPendingCopies(rhi::UploadRing& ring) const {
    if (vertices_.valid())
        ring.dropPendingFor(vertices_);
    if (indices_.valid())
        ring.dropPendingFor(indices_);
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
