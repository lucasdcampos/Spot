#include "gameplay/inventory/held_item.h"

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

void HeldItem::Serialize(Spot::Archive& archive) {
    archive.Field("displayName", displayName);
    archive.Field("icon", icon, {.tooltip = "What the HUD and the weapon wheel show for it"});
    archive.Field("model", model, {.tooltip = "What is held while it is in hand"});
}

} // namespace Gameplay
