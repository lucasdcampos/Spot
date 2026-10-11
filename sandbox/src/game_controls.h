#pragma once

#include <spot.h>

class SandboxCommands;

// Locks the cursor for the game when it starts. Pause frees it first, and quits once it is free; Fire locks it again.
// Restart starts the scene over.
class GameControls final : public Spot::Component {
public:
    GameControls();
    ~GameControls() override;
    // The scene Restart loads, which is this one.
    Spot::AssetId restartScene;
    void SetSaveDirectory(std::filesystem::path directory) { m_saveDirectory = std::move(directory); }

protected:
    void OnEnable() override;
    void OnDisable() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    // The Fire that locks the cursor is seen once every OnUpdate() has run, so it fires nothing.
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    std::unique_ptr<SandboxCommands> m_commands;
    std::filesystem::path m_saveDirectory;
};
