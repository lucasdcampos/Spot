#pragma once

#include "spot/core/math/color.h"
#include "spot/core/math/vector.h"
#include "spot/render/sky_data.h"
#include "spot/scene/component.h"

namespace Spot {

// A layer of clouds over the scene's Sky, which the wind carries across it. How much of the sky they cover, how high
// and how large they are, how soft their edges are and how the wind blows are set on the component, in the scene,
// like any other: a game only adds it beside its sky. They stay where they are in the world as the camera moves, and
// can be flown over and, drawn as a volume (Renderer3DConfig::volumetricClouds), through.
//
//     auto& sky = scene.CreateEntity("Sky");
//     sky.AddComponent<Spot::Sky>();
//     auto& clouds = sky.AddComponent<Spot::Clouds>();
//     clouds.coverage = 0.6f;
//     clouds.windSpeed = 20.0f;
//
// The sky lights them with its sun, through the air, so they redden at sunset and darken at night, where they hide the
// stars. Only the first active one of a scene is drawn; changing coverage over time makes weather. See SkyClouds.
class Clouds final : public Component {
public:
    // From 0, a clear sky, to 1, overcast.
    float coverage = 0.45f;
    // How opaque their thick parts are, from 0 to 1.
    float density = 0.85f;
    // In meters: the layer's altitude above the ground (Y 0) and about how wide a cloud is.
    float height = 1500.0f;
    float size = 900.0f;
    // How tall they rise from their altitude, in meters, drawn as a volume.
    float thickness = 1500.0f;
    // From 0, crisp edges, to 1, wispy.
    float softness = 0.35f;
    // Where the wind blows, on the ground (its Y plays no part), and how fast, in meters per second.
    Vector3 windDirection = {1.0f, 0.0f, 0.35f};
    float windSpeed = 12.0f;
    Color color = Color::White;

    // How far the wind has carried them, in meters along X and Z; it moves on as the scene updates.
    Vector2 GetOffset() const { return m_offset; }
    void SetOffset(Vector2 offset) { m_offset = offset; }
    // What the sky draws for them now.
    SkyClouds GetSkyClouds() const;

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Archive& archive) override;

private:
    Vector2 m_offset;
};

} // namespace Spot
