#pragma once

#include <cstdint>
#include <memory>

namespace Spot {

enum class BufferUsage {
    Vertex,
    Index,
    // Read by shaders as an HLSL StructuredBuffer, such as data per instance of an instanced draw; see
    // RenderPass::BindVertexStorageBuffer(). Its elements follow HLSL structured buffer layout, which packs fields as
    // C++ does, without the 16-byte rows of constant buffers.
    Storage,
};

enum class IndexType {
    UInt16,
    UInt32,
};

enum class UploadMode {
    // Keeps the rest of the buffer. The upload runs before the current frame's draws, so draws already recorded with
    // this buffer in the frame see the new data.
    Preserve,
    // The rest of the buffer may be lost. In exchange the upload never waits: if the GPU still reads the buffer, the
    // data goes to fresh memory, and draws recorded earlier keep reading what they were recorded with. Meant for data
    // rewritten every frame. Uploads that cover the whole buffer always work this way.
    Discard,
};

struct BufferDesc {
    BufferUsage usage = BufferUsage::Vertex;
    // In bytes.
    std::uint32_t size = 0;
};

// GPU memory for vertices, indices or data shaders read. Fill it with GraphicsDevice::UploadToBuffer().
class Buffer {
public:
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    BufferUsage GetUsage() const;
    std::uint32_t GetSize() const;

    // The backend's buffer (an SDL_GPUBuffer* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

private:
    friend class GraphicsDevice;

    struct Impl;

    explicit Buffer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
