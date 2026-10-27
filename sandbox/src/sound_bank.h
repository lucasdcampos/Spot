#pragma once

#include <spot.h>

// The sounds the sandbox's own code shares: the interface's click. The surfaces' sounds are in the scene's
// SurfaceLibrary, and those of effects in its EffectLibrary.
class SoundBank final : public Spot::Component {
public:
    Spot::AssetRef<Spot::Sound> click;

    // The scene's bank; null when it has none.
    static SoundBank* Find(Spot::Scene& scene);
    // Plays the click on the Interface bus, heard everywhere and also while the scenes are paused, as menus are.
    static void PlayClick(Spot::Scene& scene);

protected:
    void Serialize(Spot::Archive& archive) override;
};
