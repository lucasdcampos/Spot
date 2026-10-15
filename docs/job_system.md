# Job system

`JobSystem` (`engine/core/job_system.h`) runs work on worker threads. It is part of the core, so every layer, and
games with loops of their own, can use it.

## How it works

A system starts a worker per core but one, which is left to the thread that runs frames
(`JobSystem::GetDefaultWorkerCount()`). Programs share one system, `JobSystem::GetShared()`, so their systems do not
compete for cores; tests and tools can make their own, and a system with no workers runs every job on the threads
that wait, which helps when debugging.

- `Run(job, &counter)` queues a job. A `JobCounter` counts the unfinished jobs it was given to.
- `Wait(counter)` returns once they are done. Meanwhile the waiting thread runs queued jobs itself, so jobs can
  start and wait for jobs of their own without running out of threads.
- `ParallelFor(count, grain, fn)` calls `fn(begin, end)` over `[0, count)` in ranges of at most `grain` items, on
  the workers and the calling thread, and returns once every range is done. Pick a grain that makes each range worth
  a job: a few thousand cheap items, or a handful of costly ones.

## Who uses it

Physics steps on it: the Jolt backend runs Jolt's jobs as jobs of the shared system
(`engine/physics/jolt/jolt_job_system.h`) instead of starting a thread pool of its own, so physics and the rest of
the engine share the same workers.

## Failures

A job that throws never takes the process down. The first error among the jobs of a counter is rethrown by
`Wait()`, and by `ParallelFor()`, to the thread that waits; the other jobs still run. The error of a job nobody
waits for is logged.

## Rules for jobs

Jobs only touch the data handed to them. The game thread owns scenes, so jobs read what a system gives them and
write their own outputs, which the system applies on the game thread afterwards. A job must not wait on a lock held
by a thread that waits for it.
