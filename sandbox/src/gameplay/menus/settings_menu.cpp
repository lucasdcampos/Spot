#include "gameplay/menus/settings_menu.h"

#include <array>
#include <cmath>
#include <format>
#include <optional>

namespace Gameplay {

SPOT_COMPONENT(SettingsMenu);
SPOT_COMPONENT(BindingsMenu);

void SettingsMenu::SetPlayerName(std::string name) {
    m_settings.playerName = std::move(name);
    MarkChanged();
    Save();
}

namespace {

template <typename T>
T* Find(Spot::Scene& scene, Spot::EntityId id) {
    Spot::Entity* entity = scene.GetEntity(id);
    return entity ? entity->GetComponent<T>() : nullptr;
}

template <typename T>
T* FindChild(Spot::Scene& scene, Spot::EntityId id, std::string_view name) {
    Spot::Entity* entity = scene.GetEntity(id);
    Spot::Entity* child = entity ? entity->FindChild(name) : nullptr;
    return child ? child->GetComponent<T>() : nullptr;
}

} // namespace

bool SettingsMenu::Save() {
    if (m_directory.empty()) {
        m_dirty = false;
        return false;
    }
    if (auto* actions = Spot::Input::GetActionMap()) {
        m_settings.rebinds = actions->SaveRebindsToString();
    }
    const auto result = m_settings.Save(m_directory);
    if (!result) {
        Spot::Log::Warning("Could not save the settings: {}", result.message);
        m_saveDelay = 1.0f;
        return false;
    }
    m_dirty = false;
    return true;
}

void SettingsMenu::MarkChanged() {
    m_settings.Validate();
    m_settings.Apply(GetScene());
    UpdateLabels();
    m_dirty = true;
    m_saveDelay = 0.25f;
}

void SettingsMenu::OnMenuInitialize(Menu& menu) {
    m_settings.Read(GetScene());
    if (m_directory.empty() && GetScene().IsLoaded()) {
        m_directory = Spot::Application::Get().GetUserDataDirectory();
    }
    if (!m_directory.empty()) {
        const auto result = m_settings.Load(m_directory);
        if (!result && result.status != Spot::SaveDataStatus::NotFound) {
            Spot::Log::Warning("Could not load the settings: {}", result.message);
        }
    }
    if (auto* actions = Spot::Input::GetActionMap(); actions && !m_settings.rebinds.empty()) {
        actions->LoadRebindsFromString(m_settings.rebinds);
    }
    m_settings.Apply(GetScene());
    const auto bind = [&](Spot::EntityId id, float UserSettings::* field) {
        if (auto* slider = Find<Spot::UiSlider>(GetScene(), id)) {
            slider->SetValue(m_settings.*field);
            m_connections.push_back(slider->OnValueChanged.Connect([this, field](float value) {
                m_settings.*field = value;
                MarkChanged();
            }));
        }
    };
    bind(sensitivitySlider, &UserSettings::sensitivity);
    bind(fieldOfViewSlider, &UserSettings::fieldOfView);
    bind(masterSlider, &UserSettings::masterVolume);
    bind(effectsSlider, &UserSettings::effectsVolume);
    bind(musicSlider, &UserSettings::musicVolume);
    bind(ambienceSlider, &UserSettings::ambienceVolume);
    bind(interfaceSlider, &UserSettings::interfaceVolume);
    menu.ConnectButton(qualityButton, [this] {
        m_settings.graphicsQuality = (m_settings.graphicsQuality + 1) % 3;
        MarkChanged();
    });
    UpdateLabels();
}

void SettingsMenu::OnMenuClose(Menu& /*menu*/) {
    if (m_dirty) {
        Save();
    }
}

void SettingsMenu::AnimateMenu(Menu& menu, float blend) {
    menu.StyleControl(qualityButton, false, menu.style.text, blend);
}

void SettingsMenu::OnUpdate(float deltaTime) {
    if (!m_dirty) {
        return;
    }
    const float elapsed = Spot::Time::GetUnscaledDeltaTime() > 0 ? Spot::Time::GetUnscaledDeltaTime() : deltaTime;
    m_saveDelay -= elapsed;
    if (m_saveDelay <= 0.0f) {
        Save();
    }
}

void SettingsMenu::UpdateLabels() {
    const auto value = [&](Spot::EntityId sliderId, std::string text) {
        Spot::Entity* entity = GetScene().GetEntity(sliderId);
        Spot::Entity* parent = entity ? entity->GetParent() : nullptr;
        Spot::Entity* label = parent ? parent->FindChild("Value") : nullptr;
        if (auto* widget = label ? label->GetComponent<Spot::UiText>() : nullptr) {
            widget->text = std::move(text);
        }
    };
    value(sensitivitySlider, std::format("{:.4f}", m_settings.sensitivity));
    value(fieldOfViewSlider, std::format("{:.0f}", m_settings.fieldOfView));
    value(masterSlider, std::format("{:.0f}%", m_settings.masterVolume * 100));
    value(effectsSlider, std::format("{:.0f}%", m_settings.effectsVolume * 100));
    value(musicSlider, std::format("{:.0f}%", m_settings.musicVolume * 100));
    value(ambienceSlider, std::format("{:.0f}%", m_settings.ambienceVolume * 100));
    value(interfaceSlider, std::format("{:.0f}%", m_settings.interfaceVolume * 100));
    static constexpr std::array Names{"Low", "Medium", "High"};
    if (auto* label = Find<Spot::UiText>(GetScene(), qualityLabel)) {
        label->text = Names[static_cast<std::size_t>(m_settings.graphicsQuality)];
    }
}

void SettingsMenu::Serialize(Spot::Archive& archive) {
    archive.Field("sensitivitySlider", sensitivitySlider);
    archive.Field("fieldOfViewSlider", fieldOfViewSlider);
    archive.Field("masterSlider", masterSlider);
    archive.Field("effectsSlider", effectsSlider);
    archive.Field("musicSlider", musicSlider);
    archive.Field("ambienceSlider", ambienceSlider);
    archive.Field("interfaceSlider", interfaceSlider);
    archive.Field("qualityButton", qualityButton);
    archive.Field("qualityLabel", qualityLabel);
}

void BindingsMenu::OnMenuInitialize(Menu& menu) {
    m_accent = menu.style.accent;
    m_text = menu.style.text;
    auto* actions = Spot::Input::GetActionMap();
    Spot::Entity* parent = GetScene().GetEntity(rows);
    Spot::Entity* source = GetScene().GetEntity(rowTemplate);
    if (!actions || !parent || !source) {
        return;
    }
    const auto text = source->SaveToString();
    for (const auto& action : actions->GetActions()) {
        for (std::size_t binding = 0; binding < action.bindings.size(); ++binding) {
            const bool composite = action.bindings[binding].control.source == Spot::InputSource::Composite;
            const int parts = composite ? (action.type == Spot::InputActionType::Vector2 ? 4 : 2) : 1;
            for (int part = 0; part < parts; ++part) {
                auto created = GetScene().Instantiate(text, {.parent = parent});
                if (created.empty()) {
                    continue;
                }
                Spot::Entity& entity = *created.front();
                entity.SetActive(true);
                entity.SetName("Binding " + action.name);
                const auto row = static_cast<int>(m_rows.size());
                m_rows.push_back({action.name, binding, composite ? part : -1, entity.GetId()});
                m_rowEntities.push_back(entity.GetId());
                menu.ConnectButton(entity.GetId(), [this, row] {
                    m_rebinding = row;
                    UpdateLabels();
                });
            }
        }
    }
    if (auto* view = Find<Spot::UiScrollView>(GetScene(), scroll)) {
        if (const auto* group = Find<Spot::UiLayoutGroup>(GetScene(), rows)) {
            const std::vector<Spot::Vector2> sizes(m_rows.size(), rowSize);
            view->contentSize.y = Spot::MeasureUiLayoutGroup(group->settings, sizes).y;
        }
    }
    UpdateLabels();
}

void BindingsMenu::OnMenuOpen(Menu& /*menu*/) {
    UpdateLabels();
}

void BindingsMenu::OnMenuClose(Menu& /*menu*/) {
    m_rebinding = -1;
    UpdateLabels();
}

void BindingsMenu::AnimateMenu(Menu& menu, float blend) {
    for (const Row& row : m_rows) {
        menu.StyleControl(row.entity, false, menu.style.textSecondary, blend);
    }
}

void BindingsMenu::UpdateLabels() {
    const auto* actions = Spot::Input::GetActionMap();
    if (!actions) {
        return;
    }
    for (std::size_t index = 0; index < m_rows.size(); ++index) {
        const Row& row = m_rows[index];
        const auto* action = actions->Find(row.action);
        auto* label = FindChild<Spot::UiText>(GetScene(), row.entity, "Label");
        auto* key = FindChild<Spot::UiText>(GetScene(), row.entity, "Key");
        if (!label || !key || !action || row.binding >= action->bindings.size()) {
            continue;
        }
        const auto& binding = action->bindings[row.binding];
        const auto control = row.part >= 0 ? binding.parts[static_cast<std::size_t>(row.part)] : binding.control;
        label->font = font;
        key->font = monoFont ? monoFont : font;
        static constexpr std::array<std::string_view, 4> Directions{"Up", "Down", "Left", "Right"};
        const bool vector = row.part >= 0 && action->type == Spot::InputActionType::Vector2;
        const auto suffix = row.part < 0 ? std::string{}
                            : vector     ? std::format(" {}", Directions[static_cast<std::size_t>(row.part % 4)])
                                         : std::format(" {}", row.part + 1);
        label->text = row.action + suffix;
        const bool waiting = static_cast<int>(index) == m_rebinding;
        key->text = waiting ? "Press a control" : control.GetName();
        key->color = waiting ? m_accent : m_text;
    }
}

void BindingsMenu::OnMenuRawEvent(Menu& /*menu*/, const Spot::Event& event, bool& consumed) {
    auto* actions = Spot::Input::GetActionMap();
    if (!actions || m_rebinding < 0) {
        return;
    }
    if (const auto* key = event.GetIf<Spot::KeyPressedEvent>(); key && key->key == Spot::Key::Escape) {
        m_rebinding = -1;
        UpdateLabels();
        consumed = true;
        return;
    }
    std::optional<Spot::InputControl> control;
    if (const auto* key = event.GetIf<Spot::KeyPressedEvent>(); key && !key->repeat && key->key != Spot::Key::Unknown) {
        control = Spot::InputControl::Of(key->key);
    }
    if (const auto* mouse = event.GetIf<Spot::MouseButtonPressedEvent>()) {
        control = Spot::InputControl::Of(mouse->button);
    }
    if (const auto* pad = event.GetIf<Spot::GamepadButtonPressedEvent>()) {
        control = Spot::InputControl::Of(pad->button);
    }
    const Row& row = m_rows[static_cast<std::size_t>(m_rebinding)];
    const auto* action = actions->Find(row.action);
    const bool stick = action && row.binding < action->bindings.size() &&
                       action->bindings[row.binding].control.source == Spot::InputSource::GamepadStick;
    if (const auto* axis = event.GetIf<Spot::GamepadAxisEvent>(); axis && std::abs(axis->value) > 0.5f) {
        control = stick ? Spot::InputControl::GamepadStick(
                              axis->axis == Spot::GamepadAxis::LeftX || axis->axis == Spot::GamepadAxis::LeftY ? 0 : 1)
                        : Spot::InputControl::Of(axis->axis);
    }
    if (control) {
        if (actions->Rebind(row.action, row.binding, *control, row.part)) {
            m_rebinding = -1;
            UpdateLabels();
            if (auto* settings = GetEntity().GetComponent<SettingsMenu>()) {
                settings->MarkChanged();
            }
        }
        consumed = true;
    } else {
        consumed = event.Is<Spot::KeyPressedEvent>() || event.Is<Spot::GamepadButtonPressedEvent>() ||
                   event.Is<Spot::GamepadAxisEvent>();
    }
}

void BindingsMenu::Serialize(Spot::Archive& archive) {
    archive.Field("rows", rows, {.tooltip = "Where the rows go, with a layout group"});
    archive.Field("scroll", scroll);
    archive.Field("rowTemplate", rowTemplate);
    archive.Field("font", font);
    archive.Field("monoFont", monoFont, {.tooltip = "For the controls"});
    archive.Field("rowSize", rowSize);
}

} // namespace Gameplay
