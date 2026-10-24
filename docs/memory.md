# Memory

`engine/spot/core/memory/memory.h` gives the engine and games allocators for the patterns that `new` serves poorly, and counts
the memory they hold by what it is for.

## Allocators

- `LinearAllocator` hands memory out by moving a pointer forward and takes it all back at once with `Reset()`, as
  data that lives for a frame or a task is. It takes memory from the system in blocks, keeps them across resets and
  frees them when destroyed, so a frame that resets it allocates nothing from the system once warmed up.
  `Create<T>(count)` builds trivially destructible objects in it.
- `PoolAllocator` hands out blocks of one size, from slabs, in constant time, reusing freed blocks first. Objects of
  one type sit close together and keep their address. `ObjectPool<T>` constructs and destroys objects in its blocks.

Neither is thread-safe: give each thread its own, as the job system's jobs write into what their system hands them.

## Memory tags

Each allocator counts what it takes from the system under a `MemoryTag` (`Render`, `Physics`, `Scene`, `Game` and
the rest). `Memory::GetUsage(tag)` gives the bytes and blocks held under a tag now, which is how budgets per system
are watched. Code with allocations of its own can count them with `Memory::Track()` and `Memory::Untrack()`.
