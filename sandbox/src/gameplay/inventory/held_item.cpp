#include "gameplay/inventory/held_item.h"

#include "gameplay/core/interactor.h"
#include "gameplay/core/pawn.h"
#include "gameplay/inventory/inventory.h"

#include <algorithm>
#include <utility>

namespace Gameplay {

void ItemInput::Set(std::string_view action, bool down, bool pressed, bool released, float value) {
    auto found = std::ranges::find(m_actions, action, &Action::name);
    if (found == m_actions.end()) {
        m_actions.push_back({.name = std::string(action)});
        found = m_actions.end() - 1;
    }
    *found = {.name = std::string(action), .down = down, .pressed = pressed, .released = released, .value = value};
}

const ItemInput::Action* ItemInput::Find(std::string_view action) const {
    const auto found = std::ranges::find(m_actions, action, &Action::name);
    return found != m_actions.end() ? &*found : nullptr;
}

bool ItemInput::IsDown(std::string_view action) const {
    const Action* found = Find(action);
    return found && found->down;
}

bool ItemInput::IsPressed(std::string_view action) const {
    const Action* found = Find(action);
    return found && found->pressed;
}

bool ItemInput::IsReleased(std::string_view action) const {
    const Action* found = Find(action);
    return found && found->released;
}

float ItemInput::GetValue(std::string_view action) const {
    const Action* found = Find(action);
    return found ? found->value : 0.0f;
}

void ItemInput::Serialize(Spot::Archive& archive) {
    archive.Field("active", active);
    archive.Field("canUse", canUse);
    archive.Field("actions", m_actions);
}

void ItemInput::Action::Serialize(Spot::Archive& archive) {
    archive.Field("name", name);
    archive.Field("down", down);
    archive.Field("pressed", pressed);
    archive.Field("released", released);
    archive.Field("value", value);
}

ItemInput ItemInput::Read(const InputContext& input, bool canUse) {
    ItemInput read;
    read.active = input.IsActive();
    read.canUse = read.active && canUse;
    for (const std::string& action : input.GetDesc().actions) {
        read.Set(action, input.IsDown(action), input.IsPressed(action), input.IsReleased(action),
                 input.GetValue(action));
    }
    return read;
}

HeldItem::HeldItem(std::vector<std::string> actions)
    : m_input({.name = "Item", .priority = 10, .actions = std::move(actions)}) {
}

Inventory* HeldItem::GetInventory() const {
    const Spot::Entity* parent = GetEntity().GetParent();
    return parent ? parent->GetComponentInParent<Inventory>() : nullptr;
}

Pawn* HeldItem::GetPawn() const {
    return Pawn::Of(GetEntity());
}

ItemStatus HeldItem::DescribeStatus() const {
    return {.name = displayName, .icon = icon};
}

bool HeldItem::CanInteract(const Interactor& interactor) const {
    const auto* pawn = interactor.GetPawn();
    const auto* inventory = pawn ? pawn->GetEntity().GetComponent<Inventory>() : nullptr;
    return !GetInventory() && inventory && inventory->CanUseItem();
}

std::string HeldItem::GetPrompt(const Interactor& /*interactor*/) const {
    return "Pick up " + displayName;
}

void HeldItem::Interact(Interactor& interactor) {
    if (CanInteract(interactor)) {
        interactor.GetPawn()->GetEntity().GetComponent<Inventory>()->PickUp(*this);
    }
}

void ItemStance::Serialize(Spot::Archive& archive) {
    archive.Field("position", position);
    archive.Field("angles", angles, {.angle = true});
}

void ItemWorldPose::Serialize(Spot::Archive& archive) {
    archive.Field("enabled", enabled);
    archive.Field("ready", ready);
    archive.Field("aiming", aiming);
    archive.Field("sprint", sprint);
    archive.Field("supportOffset", supportOffset);
    archive.Field("readyAimWeight", readyAimWeight, {.min = 0.0f, .max = 1.0f});
}

void HeldItem::Serialize(Spot::Archive& archive) {
    archive.Field("displayName", displayName);
    archive.Field("icon", icon, {.tooltip = "What the HUD and the weapon wheel show for it"});
    archive.Field("worldPose", worldPose);
    archive.Field("model", model, {.tooltip = "What is held while it is in hand"});
    archive.Field("droppable", droppable);
    archive.Field("worldSize", worldSize);
    archive.Field("worldCenter", worldCenter);
    archive.Field("droppedModel", m_droppedModel);
}

} // namespace Gameplay
