#pragma once

#include <spot.h>

#include <cstdint>

// Each character gets one appearance on the server; every machine derives the same colors from its seed.
class PlayerAppearance final : public Spot::Component, public Spot::Replicated {
public:
    std::uint32_t GetSeed() const { return m_seed; }

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;
    void Replicate(Spot::Archive& archive) override;

private:
    void Apply();

    std::uint32_t m_seed = 0;
    std::uint32_t m_appliedSeed = 0;
};
