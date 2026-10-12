#include "runtime_interface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

void RuntimeInterface::SetSettingsDirectory(std::filesystem::path directory) {
    m_settingsDirectory = std::move(directory);
}
bool RuntimeInterface::SaveSettings() {
    if (m_settingsDirectory.empty()) {
        m_settingsDirty = false;
        return false;
    }
    if (auto* actions = Spot::Input::GetActionMap())
        m_settings.rebinds = actions->SaveRebindsToString();
    const auto result = m_settings.Save(m_settingsDirectory);
    if (!result) {
        Spot::Log::Warning("Could not save interface settings: {}", result.message);
        m_saveDelay = 1;
        return false;
    }
    m_settingsDirty = false;
    return true;
}
void RuntimeInterface::SettingsChanged() {
    m_settings.Validate();
    m_settings.Apply(GetScene());
    UpdateSettingsLabels();
    m_settingsDirty = true;
    m_saveDelay = 0.25f;
}
void RuntimeInterface::BindSettings() {
    auto bind = [&](Spot::EntityId id, float InterfaceSettings::* field) {
        if (auto* slider = Get<Spot::UiSlider>(id)) {
            slider->SetValue(m_settings.*field);
            m_connections.push_back(slider->OnValueChanged.Connect([this, field](float value) {
                m_settings.*field = value;
                SettingsChanged();
            }));
        }
    };
    bind(sensitivitySlider, &InterfaceSettings::sensitivity);
    bind(fovSlider, &InterfaceSettings::fieldOfView);
    bind(masterSlider, &InterfaceSettings::masterVolume);
    bind(effectsSlider, &InterfaceSettings::effectsVolume);
    bind(musicSlider, &InterfaceSettings::musicVolume);
    bind(ambienceSlider, &InterfaceSettings::ambienceVolume);
    bind(interfaceSlider, &InterfaceSettings::interfaceVolume);
    ConnectButton(qualityButton, [this] {
        m_settings.graphicsQuality = (m_settings.graphicsQuality + 1) % 3;
        SettingsChanged();
    });
    UpdateSettingsLabels();
}
void RuntimeInterface::UpdateSettingsLabels() {
    auto value = [&](Spot::EntityId sliderId, std::string text) {
        auto* entity = GetScene().GetEntity(sliderId);
        auto* parent = entity ? entity->GetParent() : nullptr;
        auto* label = parent ? parent->FindChild("Value") : nullptr;
        if (auto* widget = label ? label->GetComponent<Spot::UiText>() : nullptr)
            widget->text = std::move(text);
    };
    value(sensitivitySlider, std::format("{:.4f}", m_settings.sensitivity));
    value(fovSlider, std::format("{:.0f}", m_settings.fieldOfView));
    value(masterSlider, std::format("{:.0f}%", m_settings.masterVolume * 100));
    value(effectsSlider, std::format("{:.0f}%", m_settings.effectsVolume * 100));
    value(musicSlider, std::format("{:.0f}%", m_settings.musicVolume * 100));
    value(ambienceSlider, std::format("{:.0f}%", m_settings.ambienceVolume * 100));
    value(interfaceSlider, std::format("{:.0f}%", m_settings.interfaceVolume * 100));
    static constexpr std::array Names{"Low", "Medium", "High"};
    if (auto* label = Get<Spot::UiText>(qualityLabel))
        label->text = std::format("Graphics: {}", Names[static_cast<std::size_t>(m_settings.graphicsQuality)]);
}
void RuntimeInterface::BuildBindings() {
    auto* actions = Spot::Input::GetActionMap();
    auto* parent = GetScene().GetEntity(bindingContent);
    auto* source = GetScene().GetEntity(bindingTemplate);
    if (!actions || !parent || !source)
        return;
    const auto text = source->SaveToString();
    for (const auto& action : actions->GetActions())
        for (std::size_t binding = 0; binding < action.bindings.size(); ++binding) {
            const bool composite = action.bindings[binding].control.source == Spot::InputSource::Composite;
            const int parts = composite ? (action.type == Spot::InputActionType::Vector2 ? 4 : 2) : 1;
            for (int part = 0; part < parts; ++part) {
                auto created = GetScene().Instantiate(text, {.parent = parent});
                if (created.empty())
                    continue;
                auto& entity = *created.front();
                entity.SetActive(true);
                entity.SetName("Binding " + action.name);
                const auto row = static_cast<int>(m_bindingRows.size());
                m_bindingRows.push_back({action.name, binding, composite ? part : -1, entity.GetId()});
                ConnectButton(entity.GetId(), [this, row] {
                    m_rebinding = row;
                    UpdateBindingLabels();
                });
            }
        }
    if (auto* scroll = Get<Spot::UiScrollView>(bindingScroll)) {
        if (const auto* group = Get<Spot::UiLayoutGroup>(bindingContent)) {
            const std::vector<Spot::Vector2> sizes(m_bindingRows.size(), {410, 32});
            scroll->contentSize.y = Spot::MeasureUiLayoutGroup(group->settings, sizes).y;
        }
    }
    UpdateBindingLabels();
}
void RuntimeInterface::UpdateBindingLabels() {
    const auto* actions = Spot::Input::GetActionMap();
    if (!actions)
        return;
    for (std::size_t index = 0; index < m_bindingRows.size(); ++index) {
        const auto& row = m_bindingRows[index];
        const auto* action = actions->Find(row.action);
        auto* entity = GetScene().GetEntity(row.entity);
        auto* child = entity ? entity->FindChild("Label") : nullptr;
        auto* label = child ? child->GetComponent<Spot::UiText>() : nullptr;
        if (!label || !action || row.binding >= action->bindings.size())
            continue;
        const auto& binding = action->bindings[row.binding];
        const auto control = row.part >= 0 ? binding.parts[static_cast<std::size_t>(row.part)] : binding.control;
        label->font = font;
        const auto suffix = row.part >= 0 ? std::format(" / {}", row.part + 1) : std::string{};
        label->text = static_cast<int>(index) == m_rebinding
                          ? "Press a control (Esc cancels)"
                          : std::format("{}{}: {}", row.action, suffix, control.GetName());
    }
}
void RuntimeInterface::HandleRawEvent(const Spot::Event& event, bool& consumed) {
    auto* actions = Spot::Input::GetActionMap();
    if (!actions)
        return;
    if (m_rebinding >= 0) {
        if (const auto* key = event.GetIf<Spot::KeyPressedEvent>(); key && key->key == Spot::Key::Escape) {
            m_rebinding = -1;
            UpdateBindingLabels();
            consumed = true;
            return;
        }
        std::optional<Spot::InputControl> control;
        if (const auto* key = event.GetIf<Spot::KeyPressedEvent>();
            key && !key->repeat && key->key != Spot::Key::Unknown)
            control = Spot::InputControl::Of(key->key);
        if (const auto* mouse = event.GetIf<Spot::MouseButtonPressedEvent>())
            control = Spot::InputControl::Of(mouse->button);
        if (const auto* pad = event.GetIf<Spot::GamepadButtonPressedEvent>())
            control = Spot::InputControl::Of(pad->button);
        const auto& row = m_bindingRows[static_cast<std::size_t>(m_rebinding)];
        const auto* action = actions->Find(row.action);
        const bool stick = action && row.binding < action->bindings.size() &&
                           action->bindings[row.binding].control.source == Spot::InputSource::GamepadStick;
        if (const auto* axis = event.GetIf<Spot::GamepadAxisEvent>(); axis && std::abs(axis->value) > 0.5f)
            control =
                stick ? Spot::InputControl::GamepadStick(
                            axis->axis == Spot::GamepadAxis::LeftX || axis->axis == Spot::GamepadAxis::LeftY ? 0 : 1)
                      : Spot::InputControl::Of(axis->axis);
        if (control) {
            if (actions->Rebind(row.action, row.binding, *control, row.part)) {
                m_rebinding = -1;
                UpdateBindingLabels();
                SettingsChanged();
            }
            consumed = true;
        } else
            consumed = event.Is<Spot::KeyPressedEvent>() || event.Is<Spot::GamepadButtonPressedEvent>() ||
                       event.Is<Spot::GamepadAxisEvent>();
        return;
    }
    const auto matches = [&](const Spot::InputAction* action) {
        if (!action)
            return false;
        for (const auto& binding : action->bindings) {
            if (const auto* key = event.GetIf<Spot::KeyPressedEvent>();
                key && !key->repeat && binding.control == Spot::InputControl::Of(key->key))
                return true;
            if (const auto* pad = event.GetIf<Spot::GamepadButtonPressedEvent>();
                pad && binding.control == Spot::InputControl::Of(pad->button))
                return true;
        }
        return false;
    };
    if (m_menu == Menu::Closed) {
        if (matches(actions->Find("Spawn Menu"))) {
            OpenSpawnMenu();
            consumed = true;
        } else if (matches(actions->Find("Pause"))) {
            OpenPauseMenu();
            consumed = true;
        }
        return;
    }
    if (!GetScene().GetUiInput().WantsTextInput() &&
        matches(actions->Find(m_menu == Menu::Spawn ? "Spawn Menu" : "Pause"))) {
        CloseMenus();
        consumed = true;
    }
}
