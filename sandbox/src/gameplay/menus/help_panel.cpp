#include "gameplay/menus/help_panel.h"

#include "gameplay/hud/input_prompts.h"

#include <algorithm>
#include <format>

namespace Gameplay {

SPOT_COMPONENT(HelpPanel);

void HelpPanel::Build() {
    m_built = true;
    const auto* map = Spot::Input::GetActionMap();
    Spot::Entity* parent = GetScene().GetEntity(rows);
    Spot::Entity* source = GetScene().GetEntity(rowTemplate);
    if (!map || !parent || !source) {
        return;
    }
    const auto text = source->SaveToString();
    for (const std::string& name : actions) {
        if (!map->Find(name)) {
            continue;
        }
        auto created = GetScene().Instantiate(text, {.parent = parent});
        if (created.empty()) {
            continue;
        }
        Spot::Entity& entity = *created.front();
        entity.SetActive(true);
        entity.SetName("Help " + name);
        m_rows.push_back({name, entity.GetId()});
    }
    // The panel fits its rows, two to a line.
    if (auto* content = Get<Spot::UiRect>(rows)) {
        const auto* group = Get<Spot::UiLayoutGroup>(rows);
        const float spacing = group ? group->settings.spacing.y : 0.0f;
        const auto lines = static_cast<float>((m_rows.size() + 1) / 2);
        const float height = std::max(lines * (rowHeight + spacing) - spacing, 0.0f);
        const float extra = -content->layout.offset.y + 14.0f;
        content->layout.sizeDelta.y = height;
        if (auto* panel = GetEntity().GetComponent<Spot::UiRect>()) {
            panel->layout.sizeDelta.y = height + extra;
        }
    }
    UpdateLabels();
}

bool HelpPanel::IsWanted(const HudContext& context) {
    // Here rather than as it starts, which waits until the panel is first shown.
    if (!m_built) {
        Build();
    }
    if (Spot::Input::IsActionPressed(toggleAction)) {
        m_toggled = !m_toggled;
    }
    // The hint fades as the HUD's introduction does, and goes once the panel shows.
    const float intro = context.hud && !m_toggled ? context.hud->GetIntro() : 0.0f;
    SetActive(hint, intro > 0.0f);
    if (auto* text = Get<Spot::UiText>(hint); text && intro > 0.0f) {
        text->color.a = std::clamp(intro, 0.0f, 1.0f);
        SetText(hint, InputPrompts::Format(toggleAction, "Controls"));
    }
    return m_toggled;
}

void HelpPanel::Refresh(const HudContext& /*context*/) {
    UpdateLabels();
}

void HelpPanel::UpdateLabels() {
    const auto* map = Spot::Input::GetActionMap();
    if (!map) {
        return;
    }
    for (const Row& row : m_rows) {
        Spot::Entity* entity = GetScene().GetEntity(row.entity);
        Spot::Entity* keyEntity = entity ? entity->FindChild("Key") : nullptr;
        Spot::Entity* labelEntity = entity ? entity->FindChild("Action") : nullptr;
        const auto* action = map->Find(row.action);
        if (auto* key = keyEntity ? keyEntity->GetComponent<Spot::UiText>() : nullptr; key && action) {
            key->text = InputPrompts::GetKeyName(*action);
            key->font = monoFont ? monoFont : font;
        }
        if (auto* label = labelEntity ? labelEntity->GetComponent<Spot::UiText>() : nullptr) {
            label->text = row.action;
            label->font = font;
        }
    }
    if (Spot::Entity* close = GetEntity().FindChild("Close Hint")) {
        if (auto* text = close->GetComponent<Spot::UiText>()) {
            text->text = InputPrompts::Format(toggleAction, "Hide");
        }
    }
}

void HelpPanel::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("rows", rows, {.tooltip = "Where the rows go, with a layout group"});
    archive.Field("rowTemplate", rowTemplate);
    archive.Field("hint", hint, {.tooltip = "The line naming the key that shows the panel"});
    archive.Field("toggleAction", toggleAction);
    archive.Field("actions", actions, {.tooltip = "The actions it lists, in order"});
    archive.Field("font", font);
    archive.Field("monoFont", monoFont, {.tooltip = "For the keys"});
    archive.Field("rowHeight", rowHeight, {.min = 0.0f, .max = 200.0f});
}

} // namespace Gameplay
