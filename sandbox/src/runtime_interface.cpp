#include "runtime_interface.h"

#include "player_controller.h"
#include "prop_spawner.h"
#include "sound_bank.h"
#include "undo_list.h"

#include <algorithm>

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
void RuntimeInterface::SetHudText(std::string_view status, std::string_view hand) {
    if (auto* text = Get<Spot::UiText>(statusText); text && text->text != status)
        text->text.assign(status);
    if (auto* text = Get<Spot::UiText>(handText); text && text->text != hand)
        text->text.assign(hand);
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
}
