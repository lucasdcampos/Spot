#include "gameplay/inventory/inventory.h"
#include "gameplay/core/world_time.h"

#include "gameplay/core/player_controller.h"
#include "gameplay/inventory/held_item.h"
#include "gameplay/menus/menu.h"

#include <algorithm>
#include <cmath>
#include <typeinfo>

namespace Gameplay {

SPOT_COMPONENT(Inventory);

namespace {

// Takes away what makes an entity and those below it a body in the world, so it can be held.
void MakeHeld(Spot::Entity& entity) {
    while (entity.RemoveComponent<Spot::Rigidbody>()) {
    }
    while (entity.RemoveComponent<Spot::Collider>()) {
    }
    while (entity.RemoveComponent<Spot::MeshRenderer>()) {
    }
    for (Spot::Entity* child : entity.GetChildren()) {
        MakeHeld(*child);
    }
}

} // namespace

std::vector<HeldItem*> Inventory::GetItems() const {
    std::vector<HeldItem*> items;
    GetEntity().ForEachChild([&](const Spot::Entity& child) {
        if (HeldItem* item = child.GetComponent<HeldItem>()) {
            items.push_back(item);
        }
    });
    return items;
}

int Inventory::GetItemCount() const {
    return static_cast<int>(GetItems().size());
}

HeldItem* Inventory::GetEquipped() const {
    const std::vector<HeldItem*> items = GetItems();
    return m_selected >= 0 && m_selected < static_cast<int>(items.size()) ? items[static_cast<std::size_t>(m_selected)]
                                                                          : nullptr;
}

void Inventory::Select(int slot) {
    const int count = GetItemCount();
    if (count == 0) {
        return;
    }
    const int selected = ((slot % count) + count) % count;
    if (selected != m_selected) {
        m_selected = selected;
        PlayClick();
    }
    Equip();
}

void Inventory::Select(const HeldItem& item) {
    const std::vector<HeldItem*> items = GetItems();
    if (const auto found = std::ranges::find(items, &item); found != items.end()) {
        Select(static_cast<int>(found - items.begin()));
    }
}

HeldItem* Inventory::AddFromPrefab(const Spot::AssetRef<Spot::Prefab>& prefab) {
    Spot::Entity* entity = prefab ? GetScene().Instantiate(prefab) : nullptr;
    HeldItem* item = entity ? entity->GetComponent<HeldItem>() : nullptr;
    if (!item) {
        if (entity) {
            entity->Destroy();
        }
        return nullptr;
    }
    MakeHeld(*entity);
    // After the last item of its kind, so items of a kind stay together.
    std::size_t index = GetEntity().GetChildren().size();
    for (const HeldItem* other : GetItems()) {
        if (typeid(*other) == typeid(*item)) {
            index = other->GetEntity().GetSiblingIndex() + 1;
        }
    }
    entity->SetParent(&GetEntity());
    entity->SetSiblingIndex(index);
    entity->GetTransform().SetPosition({});
    entity->GetTransform().SetRotation({});
    // What was in hand stays in hand, wherever it now is.
    const std::vector<HeldItem*> items = GetItems();
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i]->GetEntity().GetId() == m_equipped) {
            m_selected = static_cast<int>(i);
        }
    }
    return item;
}

bool Inventory::IsStowed() const {
    const Pawn* pawn = Pawn::Of(GetEntity());
    return pawn && pawn->HasAnyState(stowedBy);
}

bool Inventory::CanUseItem() const {
    const Pawn* pawn = Pawn::Of(GetEntity());
    return IsActiveAndEnabled() && !m_wheelOpen && !IsStowed() && !(pawn && pawn->HasAnyState(blockedBy));
}

void Inventory::PlayClick() const {
    Spot::AudioDevice* device = Spot::Audio::GetDevice();
    if (device && clickSound) {
        device->Play(clickSound.GetShared(), {.volume = 0.5f, .bus = Spot::Audio::FindBus("Interface")});
    }
}

void Inventory::OpenWheel() {
    if (m_wheelOpen) {
        return;
    }
    m_wheelOpen = true;
    m_wheelSlot = m_selected;
    m_wheelPointer = {};
    m_timeScaleBefore = Spot::Time::GetTimeScale();
    WorldTime::Get(GetScene());
    // Time is everyone's in a session, so the wheel slows it only alone.
    m_timeScaleSet =
        WorldTime::IsScalingAllowed(GetScene()) ? m_timeScaleBefore * std::clamp(wheelTimeScale, 0.0f, 1.0f) : 1.0f;
    Spot::Time::SetTimeScale(m_timeScaleSet);
    // The look points it, and what is in hand is not used meanwhile.
    std::vector<std::string> actions = {lookAction};
    if (const HeldItem* item = GetEquipped()) {
        const auto& taken = item->GetInput().GetDesc().actions;
        actions.insert(actions.end(), taken.begin(), taken.end());
    }
    m_wheel.SetActions(std::move(actions));
    m_wheel.Activate(Pawn::GetControllerOf(GetEntity()));
    if (auto* pawn = Pawn::Of(GetEntity())) {
        pawn->SetState("Choosing", true);
    }
    PlayClick();
}

void Inventory::CloseWheel(bool select) {
    if (!m_wheelOpen) {
        return;
    }
    m_wheelOpen = false;
    m_wheel.Deactivate();
    if (auto* pawn = Pawn::Of(GetEntity())) {
        pawn->SetState("Choosing", false);
    }
    // Unless something else, such as the pause menu, changed it meanwhile.
    if (Spot::Time::GetTimeScale() == m_timeScaleSet) {
        Spot::Time::SetTimeScale(WorldTime::IsScalingAllowed(GetScene()) ? m_timeScaleBefore : 1.0f);
    }
    if (select) {
        Select(m_wheelSlot);
    }
}

void Inventory::UpdateWheel() {
    // A few centimeters of mouse reach the rim; the pointer stays on it as the mouse goes on.
    m_wheelPointer += m_wheel.GetVector2(lookAction) / 90.0f;
    const float length = Spot::Length(m_wheelPointer);
    if (length > 1.0f) {
        m_wheelPointer = m_wheelPointer / length;
    }
    if (length < 0.4f || wheelSlots <= 0) {
        return;
    }
    // Clockwise from the top, in sectors of the wheel.
    const float turn = std::atan2(m_wheelPointer.x, m_wheelPointer.y) / (2.0f * Spot::Pi);
    const int slot =
        (static_cast<int>(std::lround(turn * static_cast<float>(wheelSlots))) % wheelSlots + wheelSlots) % wheelSlots;
    if (slot < GetItemCount() && slot != m_wheelSlot) {
        m_wheelSlot = slot;
        PlayClick();
    }
}

void Inventory::Equip() {
    HeldItem* selected = GetEquipped();
    const Spot::EntityId id = selected ? selected->GetEntity().GetId() : Spot::EntityId::Invalid;
    if (id == m_equipped) {
        return;
    }
    if (Spot::Entity* previous = GetScene().GetEntity(m_equipped)) {
        if (HeldItem* item = previous->GetComponent<HeldItem>(); item && item->m_equipped) {
            item->m_equipped = false;
            item->m_input.Deactivate();
            item->OnUnequip();
        }
    }
    m_equipped = id;
    if (selected) {
        selected->m_equipped = true;
        selected->OnEquip();
    }
}

void Inventory::OnPlayerInput(PlayerController& player) {
    namespace Input = Spot::Input;
    std::vector<std::string> actions = selectActions;
    actions.push_back(cycleAction);
    actions.push_back(wheelAction);
    if (m_input.GetDesc().actions != actions) {
        m_input.SetActions(std::move(actions));
    }
    Controller* controller = &player;
    m_input.Activate(controller);
    const int count = GetItemCount();
    for (int slot = 0; slot < static_cast<int>(selectActions.size()) && slot < count; ++slot) {
        if (m_input.IsPressed(selectActions[static_cast<std::size_t>(slot)])) {
            Select(slot);
            m_wheelSlot = m_selected;
        }
    }
    // The wheel is for the game, not over a menu, which takes it away without choosing.
    const Spot::InputState* state = Input::GetState();
    const bool inGame = Input::IsCursorLocked() && !(state && state->IsCaptured());
    if (!m_wheelOpen && inGame && m_input.IsDown(wheelAction)) {
        OpenWheel();
    } else if (m_wheelOpen && (!inGame || IsStowed())) {
        CloseWheel(false);
    } else if (m_wheelOpen && !m_input.IsDown(wheelAction)) {
        CloseWheel(true);
    }
    if (m_wheelOpen) {
        UpdateWheel();
    }
    // An item holding something may take the cycle action, to push and pull it.
    if (m_input.IsPressed(cycleAction)) {
        Select(m_selected + (m_input.GetValue(cycleAction) > 0.0f ? 1 : -1));
    }
    Equip();

    HeldItem* item = GetEquipped();
    if (!item) {
        return;
    }
    if (IsStowed()) {
        item->m_input.Deactivate();
    } else {
        item->m_input.Activate(controller);
    }
    m_intent = ItemInput::Read(item->m_input, inGame);
    Use(m_intent);
}

void Inventory::SerializeIntent(Spot::Archive& archive) {
    int selected = m_selected;
    ItemInput intent = m_intent;
    archive.Field("selected", selected);
    archive.Field("intent", intent);
    if (archive.IsLoading()) {
        if (selected != m_selected) {
            Select(selected);
        }
        Use(intent);
    }
}

void Inventory::Use(const ItemInput& input) {
    Equip();
    const Spot::NetworkObject* object = Spot::Network::GetObject(GetEntity());
    const bool predicted = object && object->IsPredicted() && Spot::Network::IsClient(GetScene());
    if (!predicted && !Spot::Network::HasAuthority(GetEntity())) {
        return;
    }
    if (HeldItem* item = GetEquipped()) {
        ItemInput intent = input;
        intent.active = input.active && IsActiveAndEnabled() && !IsStowed();
        intent.canUse = intent.active && input.canUse && CanUseItem();
        if (predicted) {
            item->OnPredictedUse(intent);
        } else {
            item->OnUse(intent);
        }
    }
}

void Inventory::OnUpdate(float /*deltaTime*/) {
    Equip();
}

void Inventory::OnPlayerUnpossess() {
    CloseWheel(false);
    m_input.Deactivate();
    if (HeldItem* item = GetEquipped()) {
        item->m_input.Deactivate();
        item->OnUse({});
    }
}

float Inventory::GetViewZoom() const {
    const HeldItem* item = GetEquipped();
    return item && item->IsEquipped() ? item->GetZoom() : 1.0f;
}

void Inventory::OnStart() {
    // Before a menu takes the input and, pausing, the time, which the open wheel slows.
    m_menuOpening = MenuSystem::Get(GetScene()).OnOpening.Connect([this](Menu&) { CloseWheel(false); });
    Equip();
}

void Inventory::OnDisable() {
    OnPlayerUnpossess();
}

void Inventory::Replicate(Spot::Archive& archive) {
    int selected = m_selected;
    archive.Field("selected", selected);
    // Its own player's machine chooses as the player does, ahead of the server.
    const Spot::NetworkObject* object = Spot::Network::GetObject(GetEntity());
    if (archive.IsLoading() && !(object && object->IsOwnedLocally()) && selected != m_selected &&
        selected >= 0 && selected < GetItemCount()) {
        m_selected = selected;
        Equip();
    }
}

void Inventory::Serialize(Spot::Archive& archive) {
    archive.Field("selected", m_selected, {.min = 0.0f, .max = 64.0f, .tooltip = "The place of the item in hand"});
    archive.Field("wheelSlots", wheelSlots, {.min = 1.0f, .max = 16.0f});
    archive.Field("wheelTimeScale", wheelTimeScale,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "How much the open wheel slows time"});
    archive.Field("selectActions", selectActions, {.tooltip = "Each takes the item of its place"});
    archive.Field("cycleAction", cycleAction, {.tooltip = "Goes to the next item or the previous one"});
    archive.Field("wheelAction", wheelAction, {.tooltip = "Held, opens the wheel"});
    archive.Field("lookAction", lookAction, {.tooltip = "Points the open wheel"});
    archive.Field("stowedBy", stowedBy, {.tooltip = "Pawn states during which the item in hand reads nothing"});
    archive.Field("blockedBy", blockedBy, {.tooltip = "Pawn states during which the item in hand is not used"});
    archive.Field("clickSound", clickSound);
}

} // namespace Gameplay
