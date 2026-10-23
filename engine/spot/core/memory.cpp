#include "spot/core/memory.h"

#include "spot/core/assert.h"

#include <algorithm>
#include <bit>

namespace Spot {

namespace {

struct TagCounters {
    std::atomic<std::size_t> bytes = 0;
    std::atomic<std::size_t> blocks = 0;
};

std::array<TagCounters, static_cast<std::size_t>(MemoryTag::Count)>& GetCounters() {
    static std::array<TagCounters, static_cast<std::size_t>(MemoryTag::Count)> counters;
    return counters;
}

TagCounters& GetCounters(MemoryTag tag) {
    SPOT_ASSERT(tag < MemoryTag::Count, "Unknown memory tag");
    return GetCounters()[static_cast<std::size_t>(tag)];
}

std::byte* AllocateBytes(std::size_t size, std::size_t alignment, MemoryTag tag) {
    auto* bytes = static_cast<std::byte*>(::operator new(size, std::align_val_t(alignment)));
    Memory::Track(tag, size);
    return bytes;
}

void FreeBytes(std::byte* bytes, std::size_t size, std::size_t alignment, MemoryTag tag) {
    ::operator delete(bytes, size, std::align_val_t(alignment));
    Memory::Untrack(tag, size);
}

// Blocks taken from the system are aligned for anything, so allocations only need aligning within them.
constexpr std::size_t BlockAlignment = 64;

} // namespace

std::string_view GetMemoryTagName(MemoryTag tag) {
    switch (tag) {
    case MemoryTag::Other:
        return "Other";
    case MemoryTag::Core:
        return "Core";
    case MemoryTag::Render:
        return "Render";
    case MemoryTag::Physics:
        return "Physics";
    case MemoryTag::Audio:
        return "Audio";
    case MemoryTag::Animation:
        return "Animation";
    case MemoryTag::Assets:
        return "Assets";
    case MemoryTag::Scene:
        return "Scene";
    case MemoryTag::Ui:
        return "UI";
    case MemoryTag::Game:
        return "Game";
    case MemoryTag::Count:
        break;
    }
    return "Unknown";
}

void Memory::Track(MemoryTag tag, std::size_t bytes) {
    TagCounters& counters = GetCounters(tag);
    counters.bytes.fetch_add(bytes, std::memory_order_relaxed);
    counters.blocks.fetch_add(1, std::memory_order_relaxed);
}

void Memory::Untrack(MemoryTag tag, std::size_t bytes) {
    TagCounters& counters = GetCounters(tag);
    counters.bytes.fetch_sub(bytes, std::memory_order_relaxed);
    counters.blocks.fetch_sub(1, std::memory_order_relaxed);
}

MemoryUsage Memory::GetUsage(MemoryTag tag) {
    const TagCounters& counters = GetCounters(tag);
    return {.bytes = counters.bytes.load(std::memory_order_relaxed),
            .blocks = counters.blocks.load(std::memory_order_relaxed)};
}

LinearAllocator::LinearAllocator(std::size_t blockSize, MemoryTag tag)
    : m_blockSize(std::max<std::size_t>(blockSize, 256)), m_tag(tag) {
}

LinearAllocator::~LinearAllocator() {
    for (const Block& block : m_blocks) {
        FreeBytes(block.data, block.size, BlockAlignment, m_tag);
    }
}

void* LinearAllocator::Allocate(std::size_t size, std::size_t alignment) {
    SPOT_ASSERT(std::has_single_bit(alignment) && alignment <= BlockAlignment, "Alignments are powers of two up to {}",
                BlockAlignment);
    size = std::max<std::size_t>(size, 1);
    // Blocks after the current one were kept by Reset(), and are filled in turn before new ones are taken.
    while (m_current < m_blocks.size()) {
        const Block& block = m_blocks[m_current];
        const std::size_t start = (m_offset + alignment - 1) & ~(alignment - 1);
        if (start + size <= block.size) {
            m_offset = start + size;
            m_used += size;
            return block.data + start;
        }
        ++m_current;
        m_offset = 0;
    }
    const std::size_t blockSize = std::max(m_blockSize, size);
    m_blocks.push_back({AllocateBytes(blockSize, BlockAlignment, m_tag), blockSize});
    m_capacity += blockSize;
    m_current = m_blocks.size() - 1;
    m_offset = size;
    m_used += size;
    return m_blocks.back().data;
}

void LinearAllocator::Reset() {
    m_current = 0;
    m_offset = 0;
    m_used = 0;
}

PoolAllocator::PoolAllocator(std::size_t blockSize, std::size_t alignment, std::size_t blocksPerSlab, MemoryTag tag)
    : m_alignment(std::max(alignment, alignof(FreeBlock))), m_blocksPerSlab(std::max<std::size_t>(blocksPerSlab, 1)),
      m_tag(tag) {
    SPOT_ASSERT(std::has_single_bit(alignment), "Alignments are powers of two");
    // Free blocks hold the link to the next one, so every block is at least as large as that link, and a multiple
    // of the alignment so the next block is aligned too.
    const std::size_t size = std::max(blockSize, sizeof(FreeBlock));
    m_blockSize = (size + m_alignment - 1) & ~(m_alignment - 1);
}

PoolAllocator::~PoolAllocator() {
    SPOT_ASSERT(m_live == 0, "{} blocks of a pool were not given back", m_live);
    for (std::byte* slab : m_slabs) {
        FreeBytes(slab, m_blockSize * m_blocksPerSlab, m_alignment, m_tag);
    }
}

void* PoolAllocator::Allocate() {
    if (!m_free) {
        AddSlab();
    }
    FreeBlock* block = m_free;
    m_free = block->next;
    ++m_live;
    return block;
}

void PoolAllocator::Free(void* block) {
    SPOT_ASSERT(block && m_live > 0, "Only blocks of this pool can be given back");
    auto* freed = static_cast<FreeBlock*>(block);
    freed->next = m_free;
    m_free = freed;
    --m_live;
}

void PoolAllocator::AddSlab() {
    std::byte* slab = AllocateBytes(m_blockSize * m_blocksPerSlab, m_alignment, m_tag);
    m_slabs.push_back(slab);
    // Linked from the last block to the first, so blocks are handed out in address order.
    for (std::size_t i = m_blocksPerSlab; i-- > 0;) {
        auto* block = reinterpret_cast<FreeBlock*>(slab + i * m_blockSize);
        block->next = m_free;
        m_free = block;
    }
}

} // namespace Spot
