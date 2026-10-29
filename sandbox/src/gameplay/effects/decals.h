#pragma once

#include <spot.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Gameplay {

// What a mark looks like and how long it stays.
struct DecalStyle {
    // Its alpha cuts the mark out, which the color tints.
    Spot::AssetRef<Spot::Texture> texture;
    Spot::Color color = Spot::Color::White;
    float roughness = 0.5f;
    // Seconds it stays at full strength, and then fading out.
    float lifetime = 20.0f;
    float fadeTime = 3.0f;

    void Serialize(Spot::Archive& archive);
};

// Marks left on surfaces, as blood: flat squares laid on what they land on, following it as it moves, gone with it,
// and fading out after their lifetime. A fixed number of entities is kept, the oldest mark giving way to a new one when
// all are taken, so marks never pile up. They are never saved.
class Decals final : public Spot::SceneSystem {
public:
    static constexpr std::size_t MaxDecals = 256;

    Decals();

    static Decals& Get(Spot::Scene& scene);

    // Lays a mark the size across, in meters, at the point of the entity's surface, facing the normal and turned by
    // the angle around it; the stretch times as long down its texture, its +v.
    void Place(const DecalStyle& style, Spot::EntityId surface, Spot::Vector3 point, Spot::Vector3 normal, float size,
               float angle = 0.0f, float stretch = 1.0f);
    // The angle that turns a mark facing the normal to run down its texture along the direction across the surface.
    static float AngleAlong(Spot::Vector3 normal, Spot::Vector3 direction);
    std::size_t GetCount() const { return m_decals.size(); }

protected:
    void OnLateUpdate(float deltaTime) override;

private:
    struct Decal {
        Spot::EntityId entity = Spot::EntityId::Invalid;
        Spot::EntityId surface = Spot::EntityId::Invalid;
        // In the surface's space, which it is laid on, without its scale.
        Spot::Vector3 position;
        Spot::Quaternion rotation;
        Spot::Material material;
        float lifetime = 0.0f;
        float fadeTime = 0.0f;
        float age = 0.0f;
        std::uint64_t sequence = 0;
    };
    std::vector<Decal> m_decals;
    std::uint64_t m_sequence = 0;
};

} // namespace Gameplay
