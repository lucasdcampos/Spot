#include "spot/core/jobs/job_system.h"

#include "spot/core/debug/assert.h"
#include "spot/core/debug/log.h"

#include <chrono>
#include <exception>
#include <utility>

namespace Spot {

namespace {

thread_local const JobSystem* t_workerOf = nullptr;

} // namespace

std::size_t JobSystem::GetDefaultWorkerCount() {
    const unsigned cores = std::thread::hardware_concurrency();
    return cores > 1 ? cores - 1 : 1;
}

JobSystem& JobSystem::GetShared() {
    static JobSystem shared;
    return shared;
}

JobSystem::JobSystem(std::size_t workerCount) {
    m_workers.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i) {
        m_workers.emplace_back([this] { Work(); });
    }
}

JobSystem::~JobSystem() {
    {
        const std::scoped_lock lock(m_mutex);
        m_stopping = true;
    }
    m_signal.notify_all();
    for (std::thread& worker : m_workers) {
        worker.join();
    }
    // Without workers, or if they stopped first, queued jobs still run, so nothing that counts on them hangs.
    Job job;
    while (TryTake(job)) {
        Execute(job);
    }
}

bool JobSystem::IsWorkerThread() const {
    return t_workerOf == this;
}

void JobSystem::Run(std::function<void()> job, JobCounter* counter) {
    SPOT_ASSERT(job, "Only callable jobs can run");
    if (counter) {
        counter->m_pending.fetch_add(1, std::memory_order_relaxed);
    }
    {
        const std::scoped_lock lock(m_mutex);
        m_queue.push_back({std::move(job), counter});
    }
    m_signal.notify_one();
}

void JobSystem::Wait(JobCounter& counter) {
    Job job;
    while (!counter.IsDone()) {
        if (TryTake(job)) {
            Execute(job);
            continue;
        }
        std::unique_lock lock(m_mutex);
        // Woken when a job is queued or a counter reaches zero; the timeout only guards against a missed signal.
        m_signal.wait_for(lock, std::chrono::milliseconds(1), [&] { return counter.IsDone() || !m_queue.empty(); });
    }
    std::exception_ptr error;
    {
        const std::scoped_lock lock(counter.m_errorMutex);
        error = std::exchange(counter.m_error, nullptr);
    }
    if (error) {
        std::rethrow_exception(error);
    }
}

void JobSystem::Work() {
    t_workerOf = this;
    Job job;
    while (true) {
        {
            std::unique_lock lock(m_mutex);
            m_signal.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
            if (m_queue.empty()) {
                return;
            }
            job = std::move(m_queue.front());
            m_queue.pop_front();
        }
        Execute(job);
    }
}

bool JobSystem::TryTake(Job& job) {
    const std::scoped_lock lock(m_mutex);
    if (m_queue.empty()) {
        return false;
    }
    job = std::move(m_queue.front());
    m_queue.pop_front();
    return true;
}

void JobSystem::Execute(Job& job) {
    try {
        job.function();
    } catch (...) {
        if (job.counter) {
            const std::scoped_lock lock(job.counter->m_errorMutex);
            if (!job.counter->m_error) {
                job.counter->m_error = std::current_exception();
            }
        } else {
            // Nothing waits for it to rethrow the error, so it is logged rather than lost.
            try {
                throw;
            } catch (const std::exception& error) {
                Log::GetEngineLogger().Error("A job threw: {}", error.what());
            } catch (...) {
                Log::GetEngineLogger().Error("A job threw");
            }
        }
    }
    job.function = nullptr;
    if (job.counter && job.counter->m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // Taken so a waiter that just checked the counter cannot miss the signal.
        {
            const std::scoped_lock lock(m_mutex);
        }
        m_signal.notify_all();
    }
}

} // namespace Spot
