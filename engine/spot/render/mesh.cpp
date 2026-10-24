#include "spot/render/mesh.h"

#include "spot/core/debug/log.h"
#include "spot/graphics/graphics_device.h"

#include <algorithm>
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
    auto result = std::unique_ptr<Mesh>(new Mesh(std::move(vertexBuffer), std::move(indexBuffer),
                                                 static_cast<std::uint32_t>(data.indices.size()), data.GetBounds(),
                                                 data.GetSubmeshes()));
    if (!data.skin.empty()) {
        if (data.skin.size() != data.vertices.size()) {
            return nullptr;
        }
        result->m_skinBuffer = device.CreateBuffer(BufferUsage::Vertex, data.skin);
        if (!result->m_skinBuffer) {
            return nullptr;
        }
        for (const auto& vertex : data.skin) {
            for (const auto joint : vertex.joints) {
                result->m_skinJointCount = std::max(result->m_skinJointCount, joint + 1);
            }
        }
    }
    return result;
}

VertexBufferLayout Mesh::GetVertexLayout() {
    return {
        .stride = sizeof(MeshVertex),
        .attributes = {{0, VertexFormat::Float3, offsetof(MeshVertex, position)},
                       {1, VertexFormat::Float3, offsetof(MeshVertex, normal)},
                       {2, VertexFormat::Float2, offsetof(MeshVertex, uv)},
                       {3, VertexFormat::Float4, offsetof(MeshVertex, tangent)}},
    };
}

VertexBufferLayout Mesh::GetSkinLayout(std::uint32_t firstLocation) {
    return {.stride = sizeof(SkinVertex),
            .attributes = {{firstLocation, VertexFormat::UInt4, offsetof(SkinVertex, joints)},
                           {firstLocation + 1, VertexFormat::Float4, offsetof(SkinVertex, weights)}}};
}

Mesh::Mesh(std::unique_ptr<Buffer> vertexBuffer, std::unique_ptr<Buffer> indexBuffer, std::uint32_t indexCount,
           BoundingSphere bounds, std::vector<SubMesh> submeshes)
    : m_vertexBuffer(std::move(vertexBuffer)), m_indexBuffer(std::move(indexBuffer)), m_indexCount(indexCount),
      m_bounds(bounds), m_submeshes(std::move(submeshes)) {
}

Mesh::~Mesh() = default;

} // namespace Spot
