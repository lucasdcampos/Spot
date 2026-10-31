#pragma once

#include <spot.h>

// The sandbox's developer easter egg. The server unlocks it for a player's character; presentation follows on every
// machine, while PlayerInfo keeps the unlock when the player respawns.
class DeveloperMode final : public Spot::Component, public Spot::Replicated {
public:
    enum class Power { Smite, Yeet };
    Spot::EntityId rayGun = Spot::EntityId::Invalid;
    Spot::AssetRef<Spot::Model> skin;

    bool IsUnlocked() const { return m_unlocked; }
    void Unlock();
    static bool ActivateLocal(Spot::Scene& scene);
    static Spot::ConsoleResult UseLocalPower(Spot::Scene& scene, Power power, std::string_view playerName);
    // On the server: strikes the player with the power, for whoever may, as an operator, whatever their developer
    // mode; the caster, if any, is the instigator, and what a yeet throws the target away from.
    static Spot::ConsoleResult Strike(Spot::Scene& scene, Power power, std::string_view playerName,
                                      const Spot::Entity* caster = nullptr);

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;
    void Replicate(Spot::Archive& archive) override;

private:
    void Apply();
    void Bind();
    Spot::ConsoleResult UsePower(Power power, std::string_view playerName);

    bool m_unlocked = false;
    bool m_skinApplied = false;
    Spot::NetworkSystem* m_system = nullptr;
};
