#pragma once

#include "hook_lines.h"

#include <spot.h>

#include <optional>
#include <vector>

// The last item in hand (see WeaponHolder), whose mode Reload changes. Weld joins two props with a FixedJoint, and
// rope with a DistanceJoint as long as the gap between the two points clicked; each takes a click (Fire) on each prop,
// the second of which may be the level. Remover takes the prop clicked away. Everything it does goes on the UndoList.
class ToolGun final : public Spot::Component {
public:
    enum class Mode { Weld, Rope, Remover };

    Mode mode = Mode::Weld;
    float range = 100.0f;

    // The prop and point clicked first, waiting for the second click of a weld or a rope.
    bool HasFirst() const { return m_first.has_value(); }

    // What a click does, on the entity at the point, which may be the level.
    void Click(Spot::Entity& entity, Spot::Vector3 point);
    // Joins the props, the second of which may be the level; the joint goes on the first, which must have a rigidbody.
    // Each returns the joint, or null when it cannot be made.
    Spot::FixedJoint* Weld(Spot::Entity& first, Spot::Entity& second);
    Spot::DistanceJoint* Rope(Spot::Entity& first, Spot::Vector3 firstPoint, Spot::Entity& second,
                              Spot::Vector3 secondPoint);
    // Takes the prop away, unless it is part of the level, with no rigidbody; false then.
    bool Remove(Spot::Entity& entity);

protected:
    void OnUpdate(float deltaTime) override;
    void OnEnable() override;
    void OnDisable() override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Pick {
        Spot::EntityId entity;
        Spot::Vector3 point;
    };

    bool IsInHand() const;

    std::optional<Pick> m_first;
    // The ropes of the scene, drawn after its surfaces.
    std::vector<Spot::DebugLine> m_ropes;
    HookLines m_ropeLines;
    Spot::RenderHookId m_ropeHook = Spot::RenderHookId::Invalid;
};
