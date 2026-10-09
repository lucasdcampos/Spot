#pragma once

#include <spot.h>

// Locks the cursor for the game when it starts. Escape frees it first, and quits once it is free; a click locks it
// again. R starts the scene over.
class GameControls final : public Spot::Component {
public:
    // The scene R loads, which is this one.
    Spot::AssetId restartScene;

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    // The click that locks the cursor is seen once every OnUpdate() has run, so it throws nothing.
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;
};
