#include "spot/physics/jolt/jolt_job_system.h"

#include "spot/core/job_system.h"

#include <thread>

namespace Spot::Detail {

JoltJobSystem::JoltJobSystem(Spot::JobSystem& jobs, JPH::uint maxJobs, JPH::uint maxBarriers)
    : JobSystemWithBarrier(maxBarriers), m_jobs(jobs) {
    m_pool.Init(maxJobs, maxJobs);
}

JoltJobSystem::~JoltJobSystem() {
    // A barrier may execute a job before its queued wrapper runs. Those wrappers still own pool references.
    m_jobs.Wait(m_pendingJobs);
}

int JoltJobSystem::GetMaxConcurrency() const {
    // The thread stepping the simulation runs jobs too while it waits for them.
    return static_cast<int>(m_jobs.GetWorkerCount()) + 1;
}

JPH::JobHandle JoltJobSystem::CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                                        JPH::uint32 dependencyCount) {
    JPH::uint32 index = m_pool.ConstructObject(name, color, this, function, dependencyCount);
    // The pool is sized for Jolt's own limits; should it fill up, jobs in flight free their slots soon.
    while (index == decltype(m_pool)::cInvalidObjectIndex) {
        std::this_thread::yield();
        index = m_pool.ConstructObject(name, color, this, function, dependencyCount);
    }
    Job* job = &m_pool.Get(index);
    JPH::JobHandle handle(job);
    if (dependencyCount == 0) {
        QueueJob(job);
    }
    return handle;
}

void JoltJobSystem::QueueJob(Job* job) {
    // Held until the job ran, as Jolt's own thread pool does, so the slot is not freed under it.
    job->AddRef();
    m_jobs.Run(
        [job] {
            job->Execute();
            job->Release();
        },
        &m_pendingJobs);
}

void JoltJobSystem::QueueJobs(Job** jobs, JPH::uint count) {
    for (JPH::uint i = 0; i < count; ++i) {
        QueueJob(jobs[i]);
    }
}

void JoltJobSystem::FreeJob(Job* job) {
    m_pool.DestructObject(job);
}

} // namespace Spot::Detail
