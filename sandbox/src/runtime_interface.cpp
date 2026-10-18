#include "runtime_interface.h"

#include "player_controller.h"
#include "prop_spawner.h"
#include "sound_bank.h"
#include "undo_list.h"
#include "weapon_holder.h"

#include <algorithm>
#include <cmath>
#include <format>

SPOT_COMPONENT(RuntimeInterface);

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
        m_settingsDirectory = GetScene().GetApplication().GetUserDataDirectory();
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
    BindSettings();
    ConnectButton(propsTab, [this] { SelectCategory(Spawnable::Category::Prop); });
    ConnectButton(lampsTab, [this] { SelectCategory(Spawnable::Category::Lamp); });
    ConnectButton(weaponsTab, [this] { SelectCategory(Spawnable::Category::Weapon); });
    ConnectButton(spawnClose, [this] { CloseMenus(); });
    ConnectButton(pauseResume, [this] { CloseMenus(); });
    ConnectButton(pauseQuit, [this] {
        CloseMenus();
        if (GetScene().IsLoaded())
            GetScene().GetApplication().QuitGame();
    });
    ConnectButton(cleanup, [this] {
        if (auto* spawner = GetScene().FindComponent<PropSpawner>())
            spawner->Cleanup();
    });
    ConnectButton(undoSpawn, [this] {
        if (auto* undo = GetScene().FindComponent<UndoList>())
            undo->Undo();
    });
    auto& input = GetScene().GetUiInput();
    input.SetInputState(Spot::Input::GetState());
    m_connections.push_back(input.OnCancel.Connect([this](Spot::EntityId id) {
        if (id == spawnCanvas || id == pauseCanvas)
            CloseMenus();
    }));
    m_connections.push_back(input.OnRawEvent.Connect(
        [this](const Spot::Event& event, bool& consumed) { HandleRawEvent(event, consumed); }));
    m_thumbnailHook =
        GetScene().AddRenderHook(Spot::RenderStage::BeforeOpaque, [this](const Spot::RenderStageContext& stage) {
            if (m_menu == Menu::Spawn)
                m_thumbnails.Render(stage, GetScene());
        });
}
void RuntimeInterface::OnStart() {
    Initialize();
}
void RuntimeInterface::RefreshInput() {
    auto& input = GetScene().GetUiInput();
    input.SetInputState(Spot::Input::GetState());
    auto size = GetScene().GetUi().GetViewSize();
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
    m_menu = menu;
    if (auto* canvas = Get<Spot::Canvas>(menu == Menu::Spawn ? spawnCanvas : pauseCanvas))
        canvas->GetEntity().SetActive(true);
    if (menu == Menu::Pause) {
        m_previousTimeScale = Spot::Time::GetTimeScale();
        m_paused = true;
        Spot::Time::SetTimeScale(0);
    }
    RefreshInput();
    Spot::Input::UpdateActions();
    if (menu == Menu::Pause)
        GetScene().GetUiInput().Focus(pauseResume);
    else
        GetScene().GetUiInput().Focus(propsTab);
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
    if (m_settingsDirty) {
        const float elapsed = Spot::Time::GetUnscaledDeltaTime() > 0 ? Spot::Time::GetUnscaledDeltaTime() : deltaTime;
        m_saveDelay -= elapsed;
        if (m_saveDelay <= 0)
            SaveSettings();
    }
}
void RuntimeInterface::OnDisable() {
    CloseMenus();
}
void RuntimeInterface::OnDestroy() {
    CloseMenus();
    if (m_thumbnailHook != Spot::RenderHookId::Invalid)
        GetScene().RemoveRenderHook(m_thumbnailHook);
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
    constexpr Spot::Color Ink{0.95f, 0.97f, 1.0f, 1.0f};
    constexpr Spot::Color Warning{1.0f, 0.36f, 0.26f, 1.0f};
    constexpr Spot::Color Accent{1.0f, 0.68f, 0.22f, 1.0f};
    setText(statusText, state.statistics);
    setActive(statsPanel, state.showStatistics);
    setText(clockText, state.clock);
    if (auto* icon = Get<Spot::UiImage>(clockIcon)) {
        icon->texture = state.day ? sunIcon : moonIcon;
        icon->tint = state.day ? Spot::Color{1.0f, 0.8f, 0.36f, 1.0f} : Spot::Color{0.76f, 0.83f, 1.0f, 1.0f};
    }

    // What is in hand, its name a moment after switching.
    setText(handText, state.item);
    if (auto* name = Get<Spot::UiText>(handText))
        name->color = {Accent.r, Accent.g, Accent.b, std::clamp(state.itemOpacity, 0.0f, 1.0f)};
    // Without its texture, an image would fill its rectangle.
    setActive(itemIcon, static_cast<bool>(state.icon));
    if (auto* icon = Get<Spot::UiImage>(itemIcon); icon && icon->texture != state.icon)
        icon->texture = state.icon;
    setText(countText, state.count);
    setText(detailText, state.detail);
    setText(hintText, state.hints);
    setText(promptText, state.prompt);
    if (auto* count = Get<Spot::UiText>(countText)) {
        count->fontSize = state.countIsWord ? 16.0f : 28.0f;
        count->color = state.lowAmmo ? Warning : Ink;
    }
    setActive(reloadTrack, state.reload >= 0.0f);
    if (auto* fill = Get<Spot::UiRect>(reloadFill); fill && state.reload >= 0.0f)
        fill->layout.sizeDelta.x = std::clamp(state.reload, 0.0f, 1.0f) * reloadWidth;

    // Health, red when low; the edges of the screen flash as the player is hurt.
    const float health = state.maxHealth > 0.0f ? std::clamp(state.health / state.maxHealth, 0.0f, 1.0f) : 0.0f;
    setText(healthText, std::to_string(static_cast<int>(std::ceil(std::max(state.health, 0.0f)))));
    if (auto* text = Get<Spot::UiText>(healthText))
        text->color = state.lowHealth ? Warning : Ink;
    if (auto* icon = Get<Spot::UiImage>(healthIcon))
        icon->tint = state.lowHealth ? Warning : Ink;
    if (auto* fill = Get<Spot::UiRect>(healthFill))
        fill->layout.sizeDelta.x = health * healthWidth;
    if (auto* fill = Get<Spot::UiImage>(healthFill))
        fill->tint = state.lowHealth ? Warning : Ink;
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
}
void RuntimeInterface::Serialize(Spot::Archive& archive) {
    archive.Field("font", font);
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
}
