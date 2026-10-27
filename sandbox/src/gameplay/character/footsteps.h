#pragma once

#include <spot.h>

#include <cstddef>
#include <cstdint>

namespace Gameplay {

// Sounds the steps of its entity's CharacterMovement like the ground they land on, as the scene's SurfaceLibrary says
// it sounds walked on: a different one of its footsteps each time, never the same twice in a row, at a slightly
// different pitch and level.
class Footsteps : public Spot::Component {
public:
    float volume = 0.35f;

protected:
    void OnStart() override;
    void Serialize(Spot::Archive& archive) override;

private:
    void Play(float loudness);
    float Next();

    std::size_t m_footstep = 0;
    std::uint32_t m_random = 0x9e3779b9u;
    Spot::SignalConnection m_step;
};

} // namespace Gameplay
