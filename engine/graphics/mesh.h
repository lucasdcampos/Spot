#pragma once

#include "graphics/mesh_data.h"
#include "graphics/pipeline.h"

#include <cstdint>
#include <memory>

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

    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    const Buffer& GetVertexBuffer() const { return *m_vertexBuffer; }
    // Holds IndexType::UInt32 indices.
    const Buffer& GetIndexBuffer() const { return *m_indexBuffer; }
    std::uint32_t GetIndexCount() const { return m_indexCount; }
    // Around every vertex, in the mesh's own space.
    BoundingSphere GetBounds() const { return m_bounds; }

private:
    Mesh(std::unique_ptr<Buffer> vertexBuffer, std::unique_ptr<Buffer> indexBuffer, std::uint32_t indexCount,
         BoundingSphere bounds);

    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    std::uint32_t m_indexCount;
    BoundingSphere m_bounds;
};

} // namespace Spot
