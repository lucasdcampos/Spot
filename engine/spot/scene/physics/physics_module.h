#pragma once

#include "spot/runtime/module.h"

#include <memory>
#include <vector>

namespace Spot {

class ConsoleRegistration;

// 3D physics for an application: registers the physics components and the console's physics settings, and counts
// the active scene's bodies for the profiler.
class PhysicsModule final : public Module {
public:
    PhysicsModule();
    ~PhysicsModule() override;

protected:
    void OnStart() override;
    void OnStop() override;
    void OnUpdate(float deltaTime) override;

private:
    std::vector<ConsoleRegistration> m_console;
};

} // namespace Spot
