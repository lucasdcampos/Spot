#pragma once

#include <spot.h>

// G switches the shadows of every light in the scene, to compare the scene with and without them.
class ShadowToggle final : public Spot::Component {
protected:
    void OnUpdate(float deltaTime) override;
};
