#include "gameplay/inventory/inventory_elements.h"

#include "gameplay/hud/input_prompts.h"
#include "gameplay/inventory/held_item.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/inventory/weapon_wheel.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Gameplay {

SPOT_COMPONENT(ItemPanelElement);
SPOT_COMPONENT(WheelElement);

namespace {

// Seconds to fade the HUD's transient lines in or out.
constexpr float FadeTime = 0.18f;

std::string Upper(std::string_view text) {
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper;
}

Spot::Color WithAlpha(Spot::Color color, float alpha) {
    return {color.r, color.g, color.b, std::clamp(alpha, 0.0f, 1.0f)};
}

} // namespace

Inventory* ItemPanelElement::GetInventory(const HudContext& context) {
    return context.pawn ? context.pawn->GetEntity().GetComponent<Inventory>() : nullptr;
}

void ItemPanelElement::Refresh(const HudContext& context) {
    const Inventory* inventory = GetInventory(context);
    const HeldItem* item = inventory ? inventory->GetEquipped() : nullptr;
    const ItemStatus status = item ? item->DescribeStatus() : ItemStatus{};
    m_nameLeft = std::max(m_nameLeft - context.deltaTime, 0.0f);
    const Spot::EntityId id = item ? item->GetEntity().GetId() : Spot::EntityId::Invalid;
    if (id != m_lastItem) {
        // Not when the game starts, only when the player switches.
        m_nameLeft = m_started ? nameTime : 0.0f;
        m_lastItem = id;
    }
    m_started = true;
    const float itemOpacity = std::clamp(m_nameLeft / FadeTime, 0.0f, 1.0f);

    SetText(name, Upper(status.name));
    if (auto* text = Get<Spot::UiText>(name)) {
        text->color = WithAlpha(accent, itemOpacity);
    }
    // Without its texture, an image would fill its rectangle.
    SetActive(icon, static_cast<bool>(status.icon));
    if (auto* image = Get<Spot::UiImage>(icon); image && image->texture != status.icon) {
        image->texture = status.icon;
    }
    SetText(count, status.count);
    if (auto* text = Get<Spot::UiText>(count)) {
        text->fontSize = status.countIsWord ? wordSize : countSize;
        text->color = status.warning ? warningColor : textColor;
    }
    SetText(detail, status.detail);
    SetText(hints, InputPrompts::Format(status.hints));
    const float intro = context.hud ? context.hud->GetIntro() : 0.0f;
    if (auto* text = Get<Spot::UiText>(hints)) {
        text->color.a = std::clamp(std::max({itemOpacity, intro, status.urgent ? 1.0f : 0.0f}), 0.0f, 1.0f);
    }
    SetActive(reloadTrack, status.progress >= 0.0f);
    if (auto* fill = Get<Spot::UiRect>(reloadFill); fill && status.progress >= 0.0f) {
        fill->layout.sizeDelta.x = std::clamp(status.progress, 0.0f, 1.0f) * reloadWidth;
    }
}

void ItemPanelElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("name", name);
    archive.Field("icon", icon);
    archive.Field("count", count);
    archive.Field("detail", detail);
    archive.Field("hints", hints);
    archive.Field("reloadTrack", reloadTrack);
    archive.Field("reloadFill", reloadFill);
    archive.Field("reloadWidth", reloadWidth, {.min = 0, .max = 2000, .tooltip = "The progress bar's full width"});
    archive.Field("nameTime", nameTime,
                  {.min = 0, .max = 10, .speed = .05f, .tooltip = "Seconds the name of what is in hand shows"});
    archive.Field("accent", accent);
    archive.Field("textColor", textColor);
    archive.Field("warningColor", warningColor);
    archive.Field("countSize", countSize, {.min = 1, .max = 200});
    archive.Field("wordSize", wordSize, {.min = 1, .max = 200});
}

bool WheelElement::IsWanted(const HudContext& context) {
    const Inventory* inventory = ItemPanelElement::GetInventory(context);
    return inventory && inventory->IsWheelOpen();
}

void WheelElement::Refresh(const HudContext& context) {
    const Inventory* inventory = ItemPanelElement::GetInventory(context);
    auto* widget = Get<WeaponWheel>(wheel);
    if (!inventory || !widget) {
        return;
    }
    widget->slots.clear();
    for (const HeldItem* item : inventory->GetItems()) {
        const ItemStatus status = item->DescribeStatus();
        widget->slots.push_back({.name = status.name, .detail = status.summary, .icon = status.icon});
    }
    widget->highlighted = inventory->GetWheelSlot();
    widget->selected = inventory->GetSelected();
    widget->sectors = inventory->wheelSlots;
}

void WheelElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("wheel", wheel);
}

} // namespace Gameplay
