#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace {

bool IsAligned(const void* pointer, std::size_t alignment) {
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
}

} // namespace

TEST_CASE("A linear allocator hands out aligned memory and reuses its blocks after a reset", "[memory]") {
    const auto before = Spot::Memory::GetUsage(Spot::MemoryTag::Game);
    {
        Spot::LinearAllocator frame(1024, Spot::MemoryTag::Game);
        auto* bytes = static_cast<std::byte*>(frame.Allocate(3, 1));
        auto* numbers = frame.Create<double>(4);
        CHECK(IsAligned(numbers, alignof(double)));
        CHECK(reinterpret_cast<std::byte*>(numbers) >= bytes + 3);
        CHECK(numbers[3] == 0.0);
        CHECK(IsAligned(frame.Allocate(16, 64), 64));

        // Past the first block, a second one is taken; requests larger than a block get one of their own.
        frame.Allocate(1000);
        frame.Allocate(5000);
        const std::size_t capacity = frame.GetCapacity();
        CHECK(capacity >= 1024 + 1024 + 5000);
        CHECK(Spot::Memory::GetUsage(Spot::MemoryTag::Game).bytes == before.bytes + capacity);

        frame.Reset();
        CHECK(frame.GetUsedBytes() == 0);
        CHECK(frame.Allocate(8) == static_cast<void*>(bytes));
        frame.Allocate(1000);
        frame.Allocate(5000);
        CHECK(frame.GetCapacity() == capacity);
    }
    CHECK(Spot::Memory::GetUsage(Spot::MemoryTag::Game).bytes == before.bytes);
    CHECK(Spot::Memory::GetUsage(Spot::MemoryTag::Game).blocks == before.blocks);
}

TEST_CASE("A pool reuses freed blocks first and keeps every block aligned", "[memory]") {
    Spot::PoolAllocator pool(24, 16, 4, Spot::MemoryTag::Scene);
    CHECK(pool.GetBlockSize() == 32);

    std::vector<void*> blocks;
    for (int i = 0; i < 9; ++i) {
        blocks.push_back(pool.Allocate());
        CHECK(IsAligned(blocks.back(), 16));
    }
    CHECK(std::set<void*>(blocks.begin(), blocks.end()).size() == 9);
    CHECK(pool.GetLiveCount() == 9);
    CHECK(pool.GetCapacity() == 12);

    void* freed = blocks[4];
    pool.Free(freed);
    CHECK(pool.Allocate() == freed);
    for (void* block : blocks) {
        pool.Free(block);
    }
    CHECK(pool.GetLiveCount() == 0);
}

TEST_CASE("An object pool constructs and destroys objects in place", "[memory]") {
    struct Counted {
        explicit Counted(int& live, std::string name) : live(live), name(std::move(name)) { ++live; }
        ~Counted() { --live; }
        int& live;
        std::string name;
    };

    int live = 0;
    Spot::ObjectPool<Counted> pool(2);
    Counted* first = pool.Create(live, "first");
    Counted* second = pool.Create(live, "second");
    CHECK(live == 2);
    CHECK(first->name == "first");
    CHECK(pool.GetLiveCount() == 2);
    pool.Destroy(first);
    pool.Destroy(second);
    CHECK(live == 0);
    CHECK(pool.GetLiveCount() == 0);
}

TEST_CASE("Memory tags have names", "[memory]") {
    CHECK(Spot::GetMemoryTagName(Spot::MemoryTag::Render) == "Render");
    CHECK(Spot::GetMemoryTagName(Spot::MemoryTag::Ui) == "UI");
}
