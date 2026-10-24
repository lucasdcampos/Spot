#pragma once

// Backend-internal: Jolt's global state, shared by every simulation.

#include "spot/core/jobs/job_system.h"
#include "spot/physics/jolt/jolt_common.h"
#include "spot/physics/jolt/jolt_job_system.h"

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>

namespace Spot::Detail {

// The scratch memory every simulation shares, and their jobs, which run on the engine's shared job system.
// Simulations step one at a time, so they can share them.
struct JoltRuntime {
    JPH::TempAllocatorImpl tempAllocator{16 * 1024 * 1024};
    JoltJobSystem jobSystem{JobSystem::GetShared(), JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers};
};

// Keeps Jolt and the runtime alive while it exists, setting them up for the first simulation and tearing them down
// after the last.
class JoltRuntimeUser {
public:
    JoltRuntimeUser();
    ~JoltRuntimeUser();

    JoltRuntimeUser(const JoltRuntimeUser&) = delete;
    JoltRuntimeUser& operator=(const JoltRuntimeUser&) = delete;

    JoltRuntime& Get() const;
};

} // namespace Spot::Detail
