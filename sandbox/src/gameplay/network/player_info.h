#pragma once

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

struct PlayerScore {
    std::string name;
    int value = 0;
    void Serialize(Spot::Archive& archive) {
        archive.Field("name", name);
        archive.Field("value", value);
    }
};

// A player outlives the character it controls. This object is made on the server for local and remote players;
// its owner identifies the local entry on a client. PlayerInfos lists everyone, while Players stays local-only.
class PlayerInfo final : public Spot::Component, public Spot::Replicated {
public:
    std::string name = "Player";
    std::uint32_t peer = 0;
    int ping = 0;
    int team = 0;
    Spot::EntityId pawn = Spot::EntityId::Invalid;
    std::vector<PlayerScore> scores;

    void SetController(Spot::EntityId controller) { m_controller = controller; }
    Spot::EntityId GetController() const { return m_controller; }
    bool IsLocal() const;
    void Replicate(Spot::Archive& archive) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::EntityId m_controller = Spot::EntityId::Invalid;
};

namespace PlayerInfos {
std::vector<PlayerInfo*> GetAll(Spot::Scene& scene);
PlayerInfo* GetLocal(Spot::Scene& scene);
} // namespace PlayerInfos

} // namespace Gameplay
