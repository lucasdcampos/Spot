#include "graphics/mesh.h"

#include "core/log.h"
#include "graphics/graphics_device.h"

#include <cstddef>
#include <utility>

namespace Spot {

std::unique_ptr<Mesh> Mesh::Create(GraphicsDevice& device, const MeshData& data) {
    if (data.vertices.empty() || data.indices.empty()) {
        Log::GetEngineLogger().Error("Cannot create a mesh without vertices or indices");
        return nullptr;
    }

    auto vertexBuffer = device.CreateBuffer(BufferUsage::Vertex, data.vertices);
    auto indexBuffer = device.CreateBuffer(BufferUsage::Index, data.indices);
    if (!vertexBuffer || !indexBuffer) {
        return nullptr;
    }
    return std::unique_ptr<Mesh>(
        new Mesh(std::move(vertexBuffer), std::move(indexBuffer), static_cast<std::uint32_t>(data.indices.size())));
}

VertexBufferLayout Mesh::GetVertexLayout() {
    return {
        .stride = sizeof(MeshVertex),
        .attributes = {{0, VertexFormat::Float3, offsetof(MeshVertex, position)},
                       {1, VertexFormat::Float3, offsetof(MeshVertex, normal)},
                       {2, VertexFormat::Float2, offsetof(MeshVertex, uv)}},
    };
}

Mesh::Mesh(std::unique_ptr<Buffer> vertexBuffer, std::unique_ptr<Buffer> indexBuffer, std::uint32_t indexCount)
    : m_vertexBuffer(std::move(vertexBuffer)), m_indexBuffer(std::move(indexBuffer)), m_indexCount(indexCount) {
}

Mesh::~Mesh() = default;

} // namespace Spot
