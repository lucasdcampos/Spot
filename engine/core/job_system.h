#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Spot {

// Counts the unfinished jobs it was given to, so a thread can wait for them. Keeps the first error a job threw,
// which JobSystem::Wait() rethrows. It must outlive the jobs it counts.
class JobCounter {
public:
    JobCounter() = default;
    JobCounter(const JobCounter&) = delete;
    JobCounter& operator=(const JobCounter&) = delete;

    bool IsDone() const { return m_pending.load(std::memory_order_acquire) == 0; }

private:
    friend class JobSystem;

    std::atomic<std::uint32_t> m_pending = 0;
    std::mutex m_errorMutex;
    std::exception_ptr m_error;
};

// Runs jobs on worker threads, a worker per core but the one of the thread that waits. Jobs only touch the data handed
// to them, and must not wait on locks held by the threads that wait for them. A thread that waits for jobs runs queued
// ones meanwhile, so jobs can start jobs of their own and wait for them without running out of threads:
//
//     Spot::JobCounter counter;
//     jobs.Run([&] { BuildNavmesh(level); }, &counter);
//     jobs.ParallelFor(particles.size(), 1024, [&](std::size_t begin, std::size_t end) { Simulate(begin, end); });
//     jobs.Wait(counter);
//
// Programs usually share one, JobSystem::GetShared(), so their systems do not compete for cores.
class JobSystem {
public:
    // Every core but one, for the thread that runs the frame, and at least one.
    static std::size_t GetDefaultWorkerCount();
    // A system with the default worker count, made on first use and kept until the program exits.
    static JobSystem& GetShared();

    // Zero workers runs every job on the threads that wait, which helps debugging and single-core machines.
    explicit JobSystem(std::size_t workerCount = GetDefaultWorkerCount());
    // Runs the jobs still queued, then stops the workers.
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    std::size_t GetWorkerCount() const { return m_workers.size(); }
    // Whether the calling thread is one of this system's workers.
    bool IsWorkerThread() const;

    // Queues the job. With a counter, Wait() on it returns once the job is done.
    void Run(std::function<void()> job, JobCounter* counter = nullptr);
    // Runs queued jobs until the counter's jobs are done, then rethrows the first error one of them threw.
    void Wait(JobCounter& counter);

    // Calls fn(begin, end) over [0, count) in ranges of at most grain items, spread over the workers and the calling
    // thread, and returns once every range is done. Rethrows the first error a range threw.
    template <typename F>
    void ParallelFor(std::size_t count, std::size_t grain, F&& fn) {
        if (count == 0) {
            return;
        }
        grain = std::max<std::size_t>(grain, 1);
        if (count <= grain || m_workers.empty()) {
            for (std::size_t begin = 0; begin < count; begin += grain) {
                fn(begin, std::min(begin + grain, count));
            }
            return;
        }
        JobCounter counter;
        for (std::size_t begin = grain; begin < count; begin += grain) {
            const std::size_t end = std::min(begin + grain, count);
            Run([&fn, begin, end] { fn(begin, end); }, &counter);
        }
        std::exception_ptr error;
        try {
            fn(0, grain);
        } catch (...) {
            error = std::current_exception();
        }
        Wait(counter);
        if (error) {
            std::rethrow_exception(error);
        }
    }

private:
    struct Job {
        std::function<void()> function;
        JobCounter* counter = nullptr;
    };

    void Work();
    // Takes the next queued job; false when there is none.
    bool TryTake(Job& job);
    void Execute(Job& job);

    std::vector<std::thread> m_workers;
    mutable std::mutex m_mutex;
    // Signaled when jobs are queued or a counter reaches zero, and when the system stops.
    std::condition_variable m_signal;
    std::deque<Job> m_queue;
    bool m_stopping = false;
};

} // namespace Spot
