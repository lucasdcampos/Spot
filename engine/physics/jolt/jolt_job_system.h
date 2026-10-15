#pragma once

// Backend-internal: Jolt's jobs on the engine's job system, so physics shares the engine's workers instead of
// starting threads of its own.

#include "physics/jolt/jolt_common.h"

#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

namespace Spot {
class JobSystem;
} // namespace Spot

namespace Spot::Detail {

class JoltJobSystem final : public JPH::JobSystemWithBarrier {
public:
    JoltJobSystem(Spot::JobSystem& jobs, JPH::uint maxJobs, JPH::uint maxBarriers);

    int GetMaxConcurrency() const override;
    JPH::JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                             JPH::uint32 dependencyCount = 0) override;

protected:
    void QueueJob(Job* job) override;
    void QueueJobs(Job** jobs, JPH::uint count) override;
    void FreeJob(Job* job) override;

private:
    Spot::JobSystem& m_jobs;
    JPH::FixedSizeFreeList<Job> m_pool;
};

} // namespace Spot::Detail
