#pragma once

#include <spot.h>

#include <vector>

// Registrations belong to the scene's GameControls and resolve components on each execution.
class SandboxCommands {
public:
    SandboxCommands(Spot::Console& console, Spot::Scene& scene);

private:
    Spot::ConsoleResult Spawn(std::span<const std::string> arguments);
    Spot::ConsoleResult Give(std::span<const std::string> arguments);
    Spot::ConsoleResult Stress(std::span<const std::string> arguments);
    Spot::Scene& m_scene;
    std::vector<Spot::ConsoleRegistration> m_registrations;
};
