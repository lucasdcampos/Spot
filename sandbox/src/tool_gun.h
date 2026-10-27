#pragma once

#include "hook_lines.h"

#include "gameplay/inventory/held_item.h"

#include <spot.h>

class UndoList;

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// An item in hand, the last of the player's, whose mode Reload changes. Weld joins two props with a FixedJoint, and
// rope with a DistanceJoint as long as the gap between the two points clicked; each takes a click (Fire) on each prop,
// the second of which may be the level. Remover takes the prop or the lamp clicked away. Color paints the prop clicked
// in one of the palette's colors, and Material gives it one of the surfaces' materials. Lamp places a lamp where it
// is clicked, facing out of the surface, on the prop clicked so it moves with it: a point or a spot light, casting
// shadows or not, in the palette's color. Aim picks the next color, material or lamp. Everything it does goes on the
// UndoList.
class ToolGun final : public Gameplay::HeldItem {
public:
    enum class Mode { Weld, Rope, Remover, Color, Material, Lamp };
    static constexpr std::array<std::string_view, 6> ModeNames = {"Weld",  "Rope",     "Remover",
                                                                  "Color", "Material", "Lamp"};
    enum class LampKind { Point, ShadowedPoint, Spot, ShadowedSpot };
    static constexpr std::array<std::string_view, 4> LampNames = {"Point", "Point, Shadowed", "Spot", "Spot, Shadowed"};
    // Lamps carry this tag, which the remover looks for.
    static constexpr std::string_view LampTag = "Lamp";

    struct PaletteColor {
        std::string_view name;
        Spot::Color color;
    };
    static constexpr std::array<PaletteColor, 8> Palette = {{{"White", {0.9f, 0.9f, 0.9f, 1.0f}},
                                                             {"Black", {0.04f, 0.04f, 0.04f, 1.0f}},
                                                             {"Red", {0.8f, 0.06f, 0.05f, 1.0f}},
                                                             {"Orange", {0.95f, 0.4f, 0.05f, 1.0f}},
                                                             {"Yellow", {0.95f, 0.8f, 0.1f, 1.0f}},
                                                             {"Green", {0.15f, 0.6f, 0.15f, 1.0f}},
                                                             {"Blue", {0.08f, 0.25f, 0.85f, 1.0f}},
                                                             {"Purple", {0.45f, 0.12f, 0.7f, 1.0f}}}};
    // A material Material mode gives, with the name shown for it.
    struct ToolMaterial {
        std::string name;
        Spot::AssetRef<Spot::Material> material;

        void Serialize(Spot::Archive& archive);
    };

    Mode mode = Mode::Weld;
    float range = 100.0f;
    std::vector<ToolMaterial> materials;
    Spot::AssetRef<Spot::Prefab> pointLamp;
    Spot::AssetRef<Spot::Prefab> spotLamp;
    // The palette color, the material and the kind of lamp clicks give.
    int color = 0;
    int material = 0;
    LampKind lamp = LampKind::Point;

    ToolGun();

    // The prop and point clicked first, waiting for the second click of a weld or a rope.
    bool HasFirst() const { return m_first.has_value(); }

    // What a click does, on the entity at the point of its surface with the normal, which may be the level.
    void Click(Spot::Entity& entity, Spot::Vector3 point, Spot::Vector3 normal = {0.0f, 1.0f, 0.0f});
    // Joins the props, the second of which may be the level; the joint goes on the first, which must have a rigidbody.
    // Each returns the joint, or null when it cannot be made.
    Spot::FixedJoint* Weld(Spot::Entity& first, Spot::Entity& second);
    Spot::DistanceJoint* Rope(Spot::Entity& first, Spot::Vector3 firstPoint, Spot::Entity& second,
                              Spot::Vector3 secondPoint);
    // Takes the prop away, unless it is part of the level, with no rigidbody; false then.
    bool Remove(Spot::Entity& entity);
    // Each changes the prop's material, keeping the rest of it when painting, unless it is part of the level or has
    // no MeshRenderer; false then.
    bool Paint(Spot::Entity& entity, Spot::Color paint);
    bool ApplyMaterial(Spot::Entity& entity, const Spot::AssetRef<Spot::Material>& newMaterial);
    // Places a lamp of the kind on the surface at the point, facing out along the normal, below the entity unless it
    // is part of the level. Returns null if the selected prefab is unavailable or has no light.
    Spot::Entity* PlaceLamp(Spot::Entity& surface, Spot::Vector3 point, Spot::Vector3 normal);
    // Takes away the lamp nearest the point, within a little of it; false when there is none.
    bool RemoveLampNear(Spot::Vector3 point);
    // The color lamps shine in: the palette's, as bright as a light can be, or white for the darkest.
    Spot::Color GetLampColor() const;

    Gameplay::ItemStatus DescribeStatus() const override;

protected:
    void OnUse(const Gameplay::ItemInput& input) override;
    void OnUnequip() override { m_first.reset(); }
    void OnEnable() override;
    void OnDisable() override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Pick {
        Spot::EntityId entity;
        Spot::Vector3 point;
    };

    // What it does goes on the UndoList of whoever holds it.
    UndoList* GetUndo() const;
    std::optional<Pick> m_first;
    // The ropes of the scene, drawn after its surfaces.
    std::vector<Spot::DebugLine> m_ropes;
    HookLines m_ropeLines;
    Spot::RenderHookId m_ropeHook = Spot::RenderHookId::Invalid;
};
