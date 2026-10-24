#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Spot {

// What memory is for, so its use can be told apart and budgeted. The engine's allocators count what they hold under
// a tag; games can use the tags, and Other, for their own.
enum class MemoryTag {
    Other,
    Core,
    Render,
    Physics,
    Audio,
    Animation,
    Assets,
    Scene,
    Ui,
    Game,
    Count,
};

std::string_view GetMemoryTagName(MemoryTag tag);

struct MemoryUsage {
    // Bytes held now, and how many blocks of memory they are in.
    std::size_t bytes = 0;
    std::size_t blocks = 0;
};

namespace Memory {

// Counts memory held under the tag; allocators call these as they take memory from the system and give it back.
void Track(MemoryTag tag, std::size_t bytes);
void Untrack(MemoryTag tag, std::size_t bytes);
MemoryUsage GetUsage(MemoryTag tag);

} // namespace Memory

// Memory handed out by moving a pointer forward, and given back all at once with Reset(), as data that lives for a
// frame or a task is: nothing is freed one by one, and allocating costs a few instructions. It takes memory from the
// system in blocks, keeps them across resets, and gives them back when destroyed. Not thread-safe: give each thread
// its own.
//
//     Spot::LinearAllocator frame(64 * 1024, Spot::MemoryTag::Render);
//     auto* commands = frame.Create<DrawCommand>(count);
//     ...
//     frame.Reset();
class LinearAllocator {
public:
    explicit LinearAllocator(std::size_t blockSize = 64 * 1024, MemoryTag tag = MemoryTag::Other);
    ~LinearAllocator();

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    // Never null; alignment must be a power of two. Requests larger than the block size get a block of their own.
    void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

    // Default-constructs count objects; they are never destroyed, so T must be trivially destructible.
    template <typename T>
        requires std::is_trivially_destructible_v<T>
    T* Create(std::size_t count = 1) {
        T* objects = static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
        for (std::size_t i = 0; i < count; ++i) {
            new (objects + i) T();
        }
        return objects;
    }

    // Makes everything allocated since the last reset available again, keeping the blocks.
    void Reset();

    // Bytes handed out since the last reset, and bytes taken from the system.
    std::size_t GetUsedBytes() const { return m_used; }
    std::size_t GetCapacity() const { return m_capacity; }

private:
    struct Block {
        std::byte* data;
        std::size_t size;
    };

    std::vector<Block> m_blocks;
    std::size_t m_blockSize;
    std::size_t m_capacity = 0;
    std::size_t m_used = 0;
    // The block being filled, and how far.
    std::size_t m_current = 0;
    std::size_t m_offset = 0;
    MemoryTag m_tag;
};

// Blocks of one size, handed out and given back one at a time in constant time, from slabs taken from the system,
// so many objects of a type sit close together and keep their address. Freed blocks are reused first. Not
// thread-safe.
class PoolAllocator {
public:
    PoolAllocator(std::size_t blockSize, std::size_t alignment, std::size_t blocksPerSlab = 256,
                  MemoryTag tag = MemoryTag::Other);
    ~PoolAllocator();

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    void* Allocate();
    // Only blocks this pool handed out.
    void Free(void* block);

    std::size_t GetBlockSize() const { return m_blockSize; }
    // Blocks handed out and not given back.
    std::size_t GetLiveCount() const { return m_live; }
    std::size_t GetCapacity() const { return m_slabs.size() * m_blocksPerSlab; }

private:
    struct FreeBlock {
        FreeBlock* next;
    };

    void AddSlab();

    std::vector<std::byte*> m_slabs;
    FreeBlock* m_free = nullptr;
    std::size_t m_blockSize;
    std::size_t m_alignment;
    std::size_t m_blocksPerSlab;
    std::size_t m_live = 0;
    MemoryTag m_tag;
};

// A PoolAllocator for objects of one type, constructed and destroyed in place.
template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t objectsPerSlab = 256, MemoryTag tag = MemoryTag::Other)
        : m_pool(sizeof(T), alignof(T), objectsPerSlab, tag) {}

    template <typename... Args>
    T* Create(Args&&... args) {
        void* block = m_pool.Allocate();
        try {
            return new (block) T(std::forward<Args>(args)...);
        } catch (...) {
            m_pool.Free(block);
            throw;
        }
    }

    void Destroy(T* object) {
        if (object) {
            object->~T();
            m_pool.Free(object);
        }
    }

    std::size_t GetLiveCount() const { return m_pool.GetLiveCount(); }

private:
    PoolAllocator m_pool;
};

} // namespace Spot
