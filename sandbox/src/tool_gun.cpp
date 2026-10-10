#include "tool_gun.h"

#include "player_controller.h"
#include "prop_carrier.h"
#include "undo_list.h"
#include "weapon_holder.h"

#include <array>
#include <string_view>

SPOT_COMPONENT(ToolGun);

namespace {

Spot::Vector3 ToLocal(const Spot::Transform& transform, Spot::Vector3 point) {
    const Spot::Quaternion rotation = transform.GetWorldRotation();
    const Spot::Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
    const Spot::Vector3 local = inverse * (point - transform.GetWorldPosition());
    const Spot::Vector3 scale = transform.GetScale();
    return {local.x / scale.x, local.y / scale.y, local.z / scale.z};
}

Spot::Vector3 ToWorld(const Spot::Transform& transform, Spot::Vector3 point) {
    const Spot::Vector4 world = transform.GetWorldMatrix() * Spot::Vector4{point.x, point.y, point.z, 1.0f};
    return {world.x, world.y, world.z};
}

bool IsProp(const Spot::Entity& entity) {
    return entity.GetComponent<Spot::Rigidbody>() != nullptr;
}

} // namespace

void ToolGun::Click(Spot::Entity& entity, Spot::Vector3 point) {
    if (mode == Mode::Remover) {
        Remove(entity);
        return;
    }
    if (!m_first) {
        // The first click must be on a prop, which the joint goes on.
        if (IsProp(entity)) {
            m_first = Pick{entity.GetId(), point};
        }
        return;
    }
    const Pick first = *m_first;
    m_first.reset();
    Spot::Entity* firstEntity = GetScene().GetEntity(first.entity);
    if (!firstEntity || firstEntity == &entity) {
        return;
    }
    if (mode == Mode::Weld) {
        Weld(*firstEntity, entity);
    } else {
        Rope(*firstEntity, first.point, entity, point);
    }
}

Spot::FixedJoint* ToolGun::Weld(Spot::Entity& first, Spot::Entity& second) {
    if (!IsProp(first) || &first == &second) {
        return nullptr;
    }
    auto& joint = first.AddComponent<Spot::FixedJoint>();
    joint.SetConnectedEntity(second.GetId());
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushJoint(first.GetId(), first.GetComponentIndex(joint));
    }
    return &joint;
}

Spot::DistanceJoint* ToolGun::Rope(Spot::Entity& first, Spot::Vector3 firstPoint, Spot::Entity& second,
                                   Spot::Vector3 secondPoint) {
    if (!IsProp(first) || &first == &second) {
        return nullptr;
    }
    auto& joint = first.AddComponent<Spot::DistanceJoint>();
    joint.SetAnchor(ToLocal(first.GetTransform(), firstPoint));
    joint.SetConnectedEntity(second.GetId());
    joint.SetConnectedAnchor(ToLocal(second.GetTransform(), secondPoint));
    joint.SetDistance(0.0f, Spot::Distance(firstPoint, secondPoint));
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushJoint(first.GetId(), first.GetComponentIndex(joint));
    }
    return &joint;
}

bool ToolGun::Remove(Spot::Entity& entity) {
    if (!IsProp(entity)) {
        return false;
    }
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushRemoval(entity);
    }
    entity.Destroy();
    return true;
}

bool ToolGun::IsInHand() const {
    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    return holder && holder->IsToolGunSelected();
}

void ToolGun::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;
    if (!IsInHand()) {
        m_first.reset();
        return;
    }
    if (Input::IsKeyPressed(Spot::Key::R)) {
        mode = static_cast<Mode>((static_cast<int>(mode) + 1) % 3);
        m_first.reset();
    }
    const auto* player = GetEntity().GetComponent<PlayerController>();
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    if (!player || !Input::IsCursorLocked() || !Input::IsMouseButtonPressed(Spot::MouseButton::Left) ||
        (carrier && carrier->HasClick())) {
        return;
    }
    const auto hit = GetScene().GetPhysics().Raycast(player->GetEyePosition(), player->GetAimDirection(), range,
                                                     {.ignore = GetEntity().GetId()});
    if (Spot::Entity* entity = hit ? GetScene().GetEntity(hit->entity) : nullptr) {
        Click(*entity, hit->point);
    }
}

void ToolGun::OnLateUpdate(float /*deltaTime*/) {
    // Ropes, until the rendering milestones draw them as more than lines.
    const Spot::DebugDrawOptions rope{.color = {0.55f, 0.4f, 0.25f, 1.0f}};
    GetScene().ForEachComponent<Spot::DistanceJoint>([this, &rope](const Spot::DistanceJoint& joint) {
        const Spot::Entity* connected = GetScene().GetEntity(joint.GetConnectedEntity());
        const Spot::Vector3 from = ToWorld(joint.GetEntity().GetTransform(), joint.GetAnchor());
        const Spot::Vector3 to =
            connected ? ToWorld(connected->GetTransform(), joint.GetConnectedAnchor()) : joint.GetConnectedAnchor();
        Spot::DebugDraw::Line(from, to, rope);
    });
}

void ToolGun::Serialize(Spot::Archive& archive) {
    static constexpr std::array<std::string_view, 3> Modes = {"Weld", "Rope", "Remover"};
    archive.Field("mode", mode, {.enumNames = Modes});
    archive.Field("range", range, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
}
