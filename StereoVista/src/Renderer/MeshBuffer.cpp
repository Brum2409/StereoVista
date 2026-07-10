#include "Renderer/MeshBuffer.h"

#include "RHI/Device.h"
#include "RHI/UploadRing.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

namespace renderer {

namespace {

// Extra usage when the device enabled the ray-tracing extensions: the future
// RT mode (docs/TODO.md §H) builds BLAS geometry straight from these buffers
// via their device addresses — baked in at creation because usage flags are
// immutable, so meshes resident before the RT mode toggles on stay usable.
VkBufferUsageFlags rayTracingUsage(const rhi::Device& device) {
    if (!device.rayTracingSupported())
        return 0;
    return VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
}

} // namespace

void MeshBuffer::create(rhi::Device& device, const MeshData& data,
                        const char* debugName) {
    if (data.vertices.empty() || data.indices.empty())
        throw std::runtime_error("MeshBuffer: empty mesh data for '" +
                                 std::string(debugName ? debugName : "?") + "'");
    destroy();

    const VkDeviceSize vbBytes = data.vertices.size() * sizeof(Vertex);
    const VkDeviceSize ibBytes = data.indices.size() * sizeof(uint32_t);

    rhi::BufferDesc vbDesc{};
    vbDesc.size = vbBytes;
    vbDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rayTracingUsage(device);
    vbDesc.debugName = debugName;
    vertices_.create(device, vbDesc);

    rhi::BufferDesc ibDesc{};
    ibDesc.size = ibBytes;
    ibDesc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rayTracingUsage(device);
    ibDesc.debugName = debugName;
    indices_.create(device, ibDesc);

    // One staging buffer + ONE submit for both copies. Buffer::upload would
    // stage and drain the queue per buffer — two full GPU syncs per mesh adds
    // up fast on scene loads with hundreds of meshes.
    rhi::Buffer staging;
    rhi::BufferDesc stagingDesc{};
    stagingDesc.size = vbBytes + ibBytes;
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.memory = rhi::MemoryUsage::HostUpload;
    staging.create(device, stagingDesc);
    std::memcpy(staging.mapped(), data.vertices.data(), vbBytes);
    std::memcpy(static_cast<char*>(staging.mapped()) + vbBytes,
                data.indices.data(), ibBytes);
    staging.flush();

    device.immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        VkCopyBufferInfo2 copy{};
        copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copy.srcBuffer = staging.handle();
        copy.regionCount = 1;
        copy.pRegions = &region;

        region.srcOffset = 0;
        region.size = vbBytes;
        copy.dstBuffer = vertices_.handle();
        vkCmdCopyBuffer2(cmd, &copy);

        region.srcOffset = vbBytes;
        region.size = ibBytes;
        copy.dstBuffer = indices_.handle();
        vkCmdCopyBuffer2(cmd, &copy);
    });

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
    vbDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | // GpuOnly adds TRANSFER_DST
                   rayTracingUsage(device);
    vbDesc.debugName = debugName;
    vertices_.create(device, vbDesc);

    rhi::BufferDesc ibDesc{};
    ibDesc.size = VkDeviceSize(indexCount) * sizeof(uint32_t);
    ibDesc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rayTracingUsage(device);
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

rhi::VertexBinding MeshBuffer::positionUvBinding() {
    rhi::VertexBinding binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.attributes = {
        { 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) },
        { 1, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) },
    };
    return binding;
}

} // namespace renderer
