#pragma once

#include "spot/graphics/mesh_data.h"
#include "spot/graphics/pipeline.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Spot {

class Buffer;
class GraphicsDevice;

// Triangles on the GPU: MeshVertex vertices and 32-bit indices, ready to draw with Renderer3D or custom pipelines.
class Mesh {
public:
    // Returns null, after logging why, when the data is empty or the buffers cannot be created.
    static std::unique_ptr<Mesh> Create(GraphicsDevice& device, const MeshData& data);

    // The layout of the vertex buffer, for pipelines that draw meshes: position, normal and UV at locations 0, 1
    // and 2.
    static VertexBufferLayout GetVertexLayout();
    static VertexBufferLayout GetSkinLayout(std::uint32_t firstLocation = 4);
    const Buffer* GetSkinBuffer() const { return m_skinBuffer.get(); }
    std::uint32_t GetSkinJointCount() const { return m_skinJointCount; }

    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    const Buffer& GetVertexBuffer() const { return *m_vertexBuffer; }
    // Holds IndexType::UInt32 indices.
    const Buffer& GetIndexBuffer() const { return *m_indexBuffer; }
    std::uint32_t GetIndexCount() const { return m_indexCount; }
    // Its ranges of indices, at least one; see MeshData::submeshes.
    std::span<const SubMesh> GetSubmeshes() const { return m_submeshes; }
    // Draws of a mesh take the index of one of its submeshes, or this for all of it.
    static constexpr std::uint32_t AllSubmeshes = 0xFFFFFFFFu;
    // Around every vertex, in the mesh's own space.
    BoundingSphere GetBounds() const { return m_bounds; }

private:
    Mesh(std::unique_ptr<Buffer> vertexBuffer, std::unique_ptr<Buffer> indexBuffer, std::uint32_t indexCount,
         BoundingSphere bounds, std::vector<SubMesh> submeshes);

    std::unique_ptr<Buffer> m_skinBuffer;
    std::uint32_t m_skinJointCount = 0;
    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    std::uint32_t m_indexCount;
    BoundingSphere m_bounds;
    std::vector<SubMesh> m_submeshes;
};

} // namespace Spot
