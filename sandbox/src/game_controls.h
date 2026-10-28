#pragma once

#include <spot.h>

class SandboxCommands;

// The sandbox's rules: console commands and cheats, fallback NPC population and the developer tools' font.
class GameControls final : public Spot::Component {
public:
    GameControls();
    ~GameControls() override;
    Spot::AssetRef<Spot::Prefab> npcs;
    bool cheats = true;
    bool spawnNpcs = true;

protected:
    void OnEnable() override;
    void OnDisable() override;
    void OnStart() override;
    void Serialize(Spot::Archive& archive) override;

private:
    std::unique_ptr<SandboxCommands> m_commands;
    bool m_cheatsBefore = false;
};
