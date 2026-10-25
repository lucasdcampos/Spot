#include "runtime_interface.h"
#include <spot/scene/rendering/render_hook_system.h>
#include <spot/scene/ui/ui_input_system.h>
#include <spot/scene/ui/ui_system.h>

#include "interface_palette.h"
#include "player_controller.h"
#include "prop_spawner.h"
#include "sound_bank.h"
#include "undo_list.h"
#include "weapon_holder.h"

#include <algorithm>
#include <cmath>
#include <format>

SPOT_COMPONENT(RuntimeInterface);

namespace {

namespace Palette = InterfacePalette;

// Menus ease in over this many seconds, rising this many canvas pixels into place as their backdrop darkens.
constexpr float OpenDuration = 0.16f;
constexpr float OpenRise = 10.0f;
// Controls change state in about 120 milliseconds: three of these.
constexpr float StateTime = 0.04f;

void Approach(Spot::Color& color, Spot::Color target, float blend) {
    color = {std::lerp(color.r, target.r, blend), std::lerp(color.g, target.g, blend),
             std::lerp(color.b, target.b, blend), std::lerp(color.a, target.a, blend)};
}

} // namespace

void RuntimeInterface::ConnectButton(Spot::EntityId id, std::function<void()> callback) {
    if (auto* button = Get<Spot::UiButton>(id))
        m_connections.push_back(button->OnClick.Connect(std::move(callback)));
}
void RuntimeInterface::Initialize() {
    if (m_initialized)
        return;
    m_initialized = true;
    CloseMenus();
    if (const auto* player = GetScene().FindComponent<PlayerController>()) {
        m_settings.sensitivity = player->lookSensitivity;
        m_settings.fieldOfView = Spot::ToDegrees(player->GetFieldOfView());
    }
    if (auto* audio = Spot::Audio::GetDevice()) {
        m_settings.masterVolume = audio->GetMasterVolume();
        const auto volume = [&](std::string_view name, float fallback) {
            const auto bus = audio->FindBus(name);
            return bus ? audio->GetBusVolume(*bus) : fallback;
        };
        m_settings.effectsVolume = volume("Effects", m_settings.effectsVolume);
        m_settings.musicVolume = volume("Music", m_settings.musicVolume);
        m_settings.ambienceVolume = volume("Ambience", m_settings.ambienceVolume);
        m_settings.interfaceVolume = volume("Interface", m_settings.interfaceVolume);
    }
    if (m_settingsDirectory.empty() && GetScene().IsLoaded())
        m_settingsDirectory = Spot::Application::Get().GetUserDataDirectory();
    if (!m_settingsDirectory.empty()) {
        const auto result = m_settings.Load(m_settingsDirectory);
        if (!result && result.status != Spot::SaveDataStatus::NotFound)
            Spot::Log::Warning("Could not load interface settings: {}", result.message);
    }
    if (auto* actions = Spot::Input::GetActionMap(); actions && !m_settings.rebinds.empty())
        actions->LoadRebindsFromString(m_settings.rebinds);
    m_settings.Apply(GetScene());
    BuildSpawnMenu();
    BuildBindings();
    BuildHelp();
    BindSettings();
    ConnectButton(propsTab, [this] { SelectCategory(Spawnable::Category::Prop); });
    ConnectButton(lampsTab, [this] { SelectCategory(Spawnable::Category::Lamp); });
    ConnectButton(weaponsTab, [this] { SelectCategory(Spawnable::Category::Weapon); });
    ConnectButton(gridView, [this] { SetCompactView(false); });
    ConnectButton(compactView, [this] { SetCompactView(true); });
    if (auto* field = Get<Spot::UiInputField>(spawnSearch)) {
        m_connections.push_back(field->OnValueChanged.Connect([this](std::string_view text) { Search(text); }));
        // Return takes the first object found.
        m_connections.push_back(field->OnSubmit.Connect([this](std::string_view) {
            for (std::size_t index = 0; index < m_spawnButtons.size(); ++index) {
                if (const auto* card = GetScene().GetEntity(m_spawnButtons[index]); card && card->IsActiveSelf()) {
                    ChooseSpawn(index);
                    return;
                }
            }
        }));
    }
    ConnectButton(spawnClose, [this] { CloseMenus(); });
    ConnectButton(pauseResume, [this] { CloseMenus(); });
    ConnectButton(pauseQuit, [this] {
        CloseMenus();
        if (GetScene().IsLoaded())
            Spot::Application::Get().QuitGame();
    });
    ConnectButton(cleanup, [this] {
        if (auto* spawner = GetScene().FindComponent<PropSpawner>())
            spawner->Cleanup();
    });
    ConnectButton(undoSpawn, [this] {
        if (auto* undo = GetScene().FindComponent<UndoList>())
            undo->Undo();
    });
    auto& input = GetScene().GetOrAddSystem<Spot::UiInputSystem>();
    input.SetInputState(Spot::Input::GetState());
    m_connections.push_back(input.OnCancel.Connect([this](Spot::EntityId id) {
        if (id == spawnCanvas || id == pauseCanvas)
            CloseMenus();
    }));
    m_connections.push_back(input.OnRawEvent.Connect(
        [this](const Spot::Event& event, bool& consumed) { HandleRawEvent(event, consumed); }));
    m_thumbnailHook = GetScene().GetOrAddSystem<Spot::RenderHookSystem>().AddRenderHook(
        Spot::RenderStage::BeforeOpaque, [this](const Spot::RenderStageContext& stage) {
            if (m_menu == Menu::Spawn)
                m_thumbnails.Render(stage, GetScene());
        });
}
void RuntimeInterface::OnStart() {
    Initialize();
}
void RuntimeInterface::RefreshInput() {
    auto& input = GetScene().GetOrAddSystem<Spot::UiInputSystem>();
    input.SetInputState(Spot::Input::GetState());
    auto size = GetScene().GetOrAddSystem<Spot::UiSystem>().GetViewSize();
    if (size.x <= 0 || size.y <= 0)
        size = {1280, 720};
    input.Refresh(size);
}
void RuntimeInterface::Open(Menu menu) {
    Initialize();
    CloseMenus();
    // Before pausing takes the time scale, which the wheel slows while it is open.
    if (auto* holder = GetScene().FindComponent<WeaponHolder>())
        holder->CloseWheel(false);
    // Back in the menu, the prop being placed is put away; the menu still marks it.
    if (auto* spawner = GetScene().FindComponent<PropSpawner>(); spawner && menu == Menu::Spawn)
        spawner->CancelPlacement();
    m_menu = menu;
    const Spot::EntityId canvasId = menu == Menu::Spawn ? spawnCanvas : pauseCanvas;
    if (auto* canvas = Get<Spot::Canvas>(canvasId)) {
        canvas->GetEntity().SetActive(true);
        MenuLayout& layout = menu == Menu::Spawn ? m_spawnLayout : m_pauseLayout;
        if (layout.offsets.empty()) {
            canvas->GetEntity().ForEachChild([&](Spot::Entity& child) {
                if (const auto* rect = child.GetComponent<Spot::UiRect>())
                    layout.offsets.emplace_back(child.GetId(), rect->layout.offset);
            });
            if (const auto* backdrop = canvas->GetEntity().GetComponent<Spot::UiImage>())
                layout.backdrop = backdrop->tint.a;
        }
    }
    m_menuTime = 0;
    AnimateMenu(0);
    if (menu == Menu::Pause) {
        m_previousTimeScale = Spot::Time::GetTimeScale();
        m_paused = true;
        Spot::Time::SetTimeScale(0);
    }
    RefreshInput();
    Spot::Input::UpdateActions();
    if (menu == Menu::Pause)
        GetScene().GetOrAddSystem<Spot::UiInputSystem>().Focus(pauseResume);
    else
        GetScene().GetOrAddSystem<Spot::UiInputSystem>().Focus(m_category == Spawnable::Category::Prop   ? propsTab
                                                               : m_category == Spawnable::Category::Lamp ? lampsTab
                                                                                                         : weaponsTab);
    SoundBank::PlayClick(GetScene());
}
void RuntimeInterface::OpenSpawnMenu() {
    Open(Menu::Spawn);
}
void RuntimeInterface::OpenPauseMenu() {
    Open(Menu::Pause);
}
void RuntimeInterface::CloseMenus() {
    if (auto* canvas = Get<Spot::Canvas>(spawnCanvas))
        canvas->GetEntity().SetActive(false);
    if (auto* canvas = Get<Spot::Canvas>(pauseCanvas))
        canvas->GetEntity().SetActive(false);
    if (m_paused) {
        Spot::Time::SetTimeScale(m_previousTimeScale);
        m_paused = false;
    }
    m_menu = Menu::Closed;
    m_rebinding = -1;
    if (auto* input = GetScene().GetSystem<Spot::UiInputSystem>()) {
        input->ClearFocus();
        RefreshInput();
    }
    UpdateBindingLabels();
    if (m_settingsDirty)
        SaveSettings();
}
void RuntimeInterface::OnUpdate(float deltaTime) {
    if (Spot::Input::IsActionPressed("Spawn Menu") && m_menu == Menu::Closed)
        OpenSpawnMenu();
    // Menus pause the game, so they keep time unscaled.
    const float elapsed = Spot::Time::GetUnscaledDeltaTime() > 0 ? Spot::Time::GetUnscaledDeltaTime() : deltaTime;
    if (m_menu != Menu::Closed) {
        m_menuTime += elapsed;
        AnimateMenu(elapsed);
    }
    if (m_settingsDirty) {
        m_saveDelay -= elapsed;
        if (m_saveDelay <= 0)
            SaveSettings();
    }
}
void RuntimeInterface::AnimateMenu(float elapsed) {
    const bool spawn = m_menu == Menu::Spawn;
    const MenuLayout& layout = spawn ? m_spawnLayout : m_pauseLayout;
    const float t = std::clamp(m_menuTime / OpenDuration, 0.0f, 1.0f);
    const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    for (const auto& [id, offset] : layout.offsets) {
        if (auto* rect = Get<Spot::UiRect>(id))
            rect->layout.offset = offset - Spot::Vector2{0.0f, OpenRise * (1.0f - eased)};
    }
    if (auto* backdrop = Get<Spot::UiImage>(spawn ? spawnCanvas : pauseCanvas))
        backdrop->tint.a = layout.backdrop * eased;

    // The selected control is outlined in the accent, the focused one in white, the one under the pointer faintly.
    const float blend = elapsed > 0 ? 1.0f - std::exp(-elapsed / StateTime) : 0.0f;
    const auto style = [&](Spot::EntityId id, bool selected, Spot::Color rest) {
        const auto* widget = Get<Spot::UiWidget>(id);
        if (!widget || !widget->GetEntity().IsActiveInHierarchy())
            return;
        const auto state = widget->GetUiState();
        const bool hot = state == Spot::UiControlState::Hovered || state == Spot::UiControlState::Pressed;
        const bool focused = state == Spot::UiControlState::Focused;
        if (auto* outline = GetChild<Spot::UiImage>(id, "Outline"))
            Approach(outline->tint,
                     selected  ? accent
                     : focused ? Palette::LineFocused
                     : hot     ? Palette::LineHovered
                               : Palette::Line,
                     blend);
        if (auto* label = GetChild<Spot::UiText>(id, "Label"))
            Approach(label->color, selected || hot || focused ? Palette::Text : rest, blend);
        if (auto* icon = GetChild<Spot::UiImage>(id, "Icon"))
            Approach(icon->tint, selected ? accent : hot || focused ? Palette::Text : Palette::TextSecondary, blend);
    };
    if (spawn) {
        for (std::size_t index = 0; index < m_spawnButtons.size(); ++index)
            style(m_spawnButtons[index], m_chosen == index, Palette::Text);
        style(propsTab, m_category == Spawnable::Category::Prop, Palette::TextSecondary);
        style(lampsTab, m_category == Spawnable::Category::Lamp, Palette::TextSecondary);
        style(weaponsTab, m_category == Spawnable::Category::Weapon, Palette::TextSecondary);
        style(gridView, !m_compact, Palette::TextSecondary);
        style(compactView, m_compact, Palette::TextSecondary);
        for (const auto id : {undoSpawn, cleanup, spawnClose})
            style(id, false, Palette::TextSecondary);
        const auto* field = Get<Spot::UiWidget>(spawnSearch);
        const bool typing = field && field->GetUiState() == Spot::UiControlState::Focused;
        const bool hot = field && field->GetUiState() == Spot::UiControlState::Hovered;
        if (auto* outline = GetChild<Spot::UiImage>(spawnSearchFrame, "Outline"))
            Approach(outline->tint, typing ? Palette::LineFocused : hot ? Palette::LineHovered : Palette::Line, blend);
        if (auto* icon = GetChild<Spot::UiImage>(spawnSearchFrame, "Icon"))
            Approach(icon->tint, typing ? Palette::Text : Palette::TextMuted, blend);
    } else {
        for (const auto& row : m_bindingRows)
            style(row.entity, false, Palette::TextSecondary);
        style(qualityButton, false, Palette::Text);
        style(pauseQuit, false, Palette::TextSecondary);
    }
}
void RuntimeInterface::OnDisable() {
    CloseMenus();
}
void RuntimeInterface::OnDestroy() {
    CloseMenus();
    if (m_thumbnailHook != Spot::RenderHookId::Invalid)
        GetScene().GetOrAddSystem<Spot::RenderHookSystem>().RemoveRenderHook(m_thumbnailHook);
    m_connections.clear();
}
void RuntimeInterface::UpdateHud(const HudState& state) {
    const auto setText = [this](Spot::EntityId id, std::string_view value) {
        if (auto* text = Get<Spot::UiText>(id); text && text->text != value)
            text->text.assign(value);
    };
    const auto setActive = [this](Spot::EntityId id, bool active) {
        if (auto* entity = GetScene().GetEntity(id); entity && entity->IsActiveSelf() != active)
            entity->SetActive(active);
    };
    const auto setOpacity = [this](Spot::EntityId id, float opacity) {
        if (auto* text = Get<Spot::UiText>(id))
            text->color.a = std::clamp(opacity, 0.0f, 1.0f);
    };
    const float elapsed = Spot::Time::GetUnscaledDeltaTime();
    setText(statusText, state.statistics);
    setActive(statsPanel, state.showStatistics);
    setText(clockText, state.clock);
    if (auto* icon = Get<Spot::UiImage>(clockIcon))
        icon->texture = state.day ? sunIcon : moonIcon;

    // What is in hand, its name a moment after switching, and its controls while they matter.
    setText(handText, state.item);
    if (auto* name = Get<Spot::UiText>(handText))
        name->color = Palette::WithAlpha(accent, std::clamp(state.itemOpacity, 0.0f, 1.0f));
    // Without its texture, an image would fill its rectangle.
    setActive(itemIcon, static_cast<bool>(state.icon));
    if (auto* icon = Get<Spot::UiImage>(itemIcon); icon && icon->texture != state.icon)
        icon->texture = state.icon;
    setText(countText, state.count);
    setText(detailText, state.detail);
    setText(hintText, state.hints);
    setOpacity(hintText, state.hintOpacity);
    setText(promptText, state.prompt);
    if (auto* count = Get<Spot::UiText>(countText)) {
        count->fontSize = state.countIsWord ? 15.0f : 28.0f;
        count->color = state.lowAmmo ? Palette::Warning : Palette::Text;
    }
    setActive(reloadTrack, state.reload >= 0.0f);
    if (auto* fill = Get<Spot::UiRect>(reloadFill); fill && state.reload >= 0.0f)
        fill->layout.sizeDelta.x = std::clamp(state.reload, 0.0f, 1.0f) * reloadWidth;

    // Health, red when low, its bar easing to the new value; the edges of the screen flash as the player is hurt.
    const float health = state.maxHealth > 0.0f ? std::clamp(state.health / state.maxHealth, 0.0f, 1.0f) : 0.0f;
    m_healthShown = m_healthShown < 0.0f || elapsed <= 0.0f
                        ? health
                        : std::lerp(m_healthShown, health, 1.0f - std::exp(-elapsed / StateTime));
    setText(healthText, std::to_string(static_cast<int>(std::ceil(std::max(state.health, 0.0f)))));
    const Spot::Color healthColor = state.lowHealth ? Palette::Warning : Palette::Text;
    if (auto* text = Get<Spot::UiText>(healthText))
        text->color = healthColor;
    if (auto* icon = Get<Spot::UiImage>(healthIcon))
        icon->tint = healthColor;
    if (auto* fill = Get<Spot::UiRect>(healthFill))
        fill->layout.sizeDelta.x = m_healthShown * healthWidth;
    if (auto* fill = Get<Spot::UiImage>(healthFill))
        fill->tint = healthColor;
    setActive(damageOverlay, state.damage > 0.01f);
    if (auto* overlay = Get<Spot::UiImage>(damageOverlay))
        overlay->tint.a = std::clamp(state.damage, 0.0f, 1.0f);
    setActive(deathOverlay, state.dead);
    if (state.dead)
        setText(deathTimer, std::format("Respawning in {}", static_cast<int>(std::ceil(state.respawn))));

    setActive(crosshair, state.crosshair);
    if (auto* entity = GetScene().GetEntity(crosshair); entity && state.crosshair) {
        // The lines move out as the aim gets less steady.
        const float gap = 7.0f + std::clamp(state.spread, 0.0f, 1.0f) * 14.0f;
        const std::pair<std::string_view, Spot::Vector2> lines[] = {
            {"Top", {0, gap}}, {"Bottom", {0, -gap}}, {"Left", {-gap, 0}}, {"Right", {gap, 0}}};
        for (const auto& [name, position] : lines) {
            if (auto* line = entity->FindChild(name)) {
                if (auto* rect = line->GetComponent<Spot::UiRect>())
                    rect->layout.offset = position;
            }
        }
    }
    if (auto* marker = Get<Spot::UiImage>(hitMarker))
        marker->tint.a = std::clamp(state.hitMarker, 0.0f, 1.0f);

    setActive(wheelOverlay, state.wheelOpen);
    if (auto* wheel = Get<WeaponWheel>(weaponWheel); wheel && state.wheelOpen) {
        wheel->slots = state.slots;
        wheel->highlighted = state.wheelSlot;
        wheel->selected = state.selectedSlot;
    }

    setActive(helpPanel, state.showHelp);
    setActive(helpHint, state.helpHintOpacity > 0.0f);
    setOpacity(helpHint, state.helpHintOpacity);
}
void RuntimeInterface::Serialize(Spot::Archive& archive) {
    archive.Field("font", font);
    archive.Field("monoFont", monoFont, {.tooltip = "For numbers and keys"});
    archive.Field("titleFont", titleFont, {.tooltip = "For titles and the selected category"});
    archive.Field("accent", accent, {.tooltip = "What is selected: the object last chosen, the category shown"});
    archive.Field("spawnCanvas", spawnCanvas);
    archive.Field("pauseCanvas", pauseCanvas);
    archive.Field("spawnGrid", spawnGrid);
    archive.Field("spawnScroll", spawnScroll);
    archive.Field("spawnTemplate", spawnTemplate);
    archive.Field("propsTab", propsTab);
    archive.Field("lampsTab", lampsTab);
    archive.Field("weaponsTab", weaponsTab);
    archive.Field("spawnClose", spawnClose);
    archive.Field("cleanup", cleanup);
    archive.Field("undoSpawn", undoSpawn);
    archive.Field("spawnSearch", spawnSearch);
    archive.Field("spawnSearchFrame", spawnSearchFrame);
    archive.Field("spawnCount", spawnCount);
    archive.Field("gridView", gridView);
    archive.Field("compactView", compactView);
    archive.Field("pauseResume", pauseResume);
    archive.Field("pauseQuit", pauseQuit);
    archive.Field("qualityButton", qualityButton);
    archive.Field("qualityLabel", qualityLabel);
    archive.Field("bindingContent", bindingContent);
    archive.Field("bindingScroll", bindingScroll);
    archive.Field("bindingTemplate", bindingTemplate);
    archive.Field("sensitivitySlider", sensitivitySlider);
    archive.Field("fovSlider", fovSlider);
    archive.Field("masterSlider", masterSlider);
    archive.Field("effectsSlider", effectsSlider);
    archive.Field("musicSlider", musicSlider);
    archive.Field("ambienceSlider", ambienceSlider);
    archive.Field("interfaceSlider", interfaceSlider);
    archive.Field("statusText", statusText);
    archive.Field("handText", handText);
    archive.Field("statsPanel", statsPanel);
    archive.Field("clockText", clockText);
    archive.Field("countText", countText);
    archive.Field("detailText", detailText);
    archive.Field("hintText", hintText);
    archive.Field("reloadTrack", reloadTrack);
    archive.Field("reloadFill", reloadFill);
    archive.Field("reloadWidth", reloadWidth, {.min = 0, .max = 2000, .tooltip = "The reload bar's full width"});
    archive.Field("healthWidth", healthWidth, {.min = 0, .max = 2000, .tooltip = "The health bar's full width"});
    archive.Field("crosshair", crosshair);
    archive.Field("hitMarker", hitMarker);
    archive.Field("promptText", promptText);
    archive.Field("itemIcon", itemIcon);
    archive.Field("clockIcon", clockIcon);
    archive.Field("sunIcon", sunIcon);
    archive.Field("moonIcon", moonIcon);
    archive.Field("healthIcon", healthIcon);
    archive.Field("healthText", healthText);
    archive.Field("healthFill", healthFill);
    archive.Field("damageOverlay", damageOverlay);
    archive.Field("deathOverlay", deathOverlay);
    archive.Field("deathTimer", deathTimer);
    archive.Field("wheelOverlay", wheelOverlay);
    archive.Field("weaponWheel", weaponWheel);
    archive.Field("helpPanel", helpPanel);
    archive.Field("helpContent", helpContent);
    archive.Field("helpTemplate", helpTemplate);
    archive.Field("helpHint", helpHint);
}
