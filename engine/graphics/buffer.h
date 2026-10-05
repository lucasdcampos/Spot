#pragma once

#include <cstdint>
#include <memory>

namespace Spot {

enum class BufferUsage {
    Vertex,
    Index,
};

enum class IndexType {
    UInt16,
    UInt32,
};

struct BufferDesc {
    BufferUsage usage = BufferUsage::Vertex;
    // In bytes.
    std::uint32_t size = 0;
};

// GPU memory for vertices or indices. Fill it with GraphicsDevice::UploadToBuffer().
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
