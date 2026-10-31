#include "main_menu.h"

#include "gameplay/game/map_info.h"
#include "gameplay/menus/settings_menu.h"
#include "gameplay/network/session.h"
#include <spot/scene/rendering/render_hook_system.h>
#include <spot/scene/ui/ui_input_system.h>

#include <algorithm>
#include <cmath>
#include <optional>

SPOT_COMPONENT(MainMenu);

namespace {

// How dark the map behind each page is: lighter where it is the subject, dark under the settings' many controls.
float GetShade(MainMenu::Page page) {
    switch (page) {
    case MainMenu::Page::Home:
        return 0.72f;
    case MainMenu::Page::Play:
    case MainMenu::Page::Host:
    case MainMenu::Page::Join:
        return 0.66f;
    case MainMenu::Page::Connecting:
        return 0.9f;
    case MainMenu::Page::Settings:
        return 0.95f;
    case MainMenu::Page::Quit:
        return 0.9f;
    }
    return 0.72f;
}

Spot::Color Mix(Spot::Color from, Spot::Color to, float blend) {
    return {std::lerp(from.r, to.r, blend), std::lerp(from.g, to.g, blend), std::lerp(from.b, to.b, blend),
            std::lerp(from.a, to.a, blend)};
}

} // namespace

void MainMenu::DiscoverMaps() {
    Spot::AssetManager* assets = Spot::Assets::GetManager();
    Spot::Entity* source = GetScene().GetEntity(mapButtonTemplate);
    Spot::Entity* parent = GetScene().GetEntity(mapScroll);
    if (!assets || !source || !parent) {
        return;
    }
    Gameplay::MapCatalog catalog;
    catalog.Refresh(*assets, mapDirectory);
    const std::string text = source->SaveToString();
    source->SetActive(false);
    for (const Gameplay::MapEntry& entry : catalog.GetEntries()) {
        const auto created = GetScene().Instantiate(text, {.parent = parent, .assets = assets});
        if (created.empty()) {
            continue;
        }
        Spot::Entity& row = *created.front();
        row.SetActive(true);
        row.SetName(entry.name);
        if (auto* rect = row.GetComponent<Spot::UiRect>()) {
            rect->layout.anchorMin = rect->layout.anchorMax = {0.0f, 1.0f};
            rect->layout.offset = {18.0f, -20.0f - 48.0f * static_cast<float>(maps.size())};
        }
        if (auto* label = GetChild<Spot::UiText>(row.GetId(), "Label")) {
            label->text = entry.name;
        }
        MainMenuMap map;
        static_cast<Gameplay::MapEntry&>(map) = entry;
        map.button = row.GetId();
        maps.push_back(std::move(map));
    }
    if (auto* scroll = parent->GetComponent<Spot::UiScrollView>()) {
        scroll->contentSize.y = static_cast<float>(maps.size()) * 48.0f;
    }
    if (auto* start = Get<Spot::UiButton>(startButton)) {
        start->SetInteractable(!maps.empty());
    }
}

void MainMenu::OnStart() {
    Spot::Input::SetCursorLocked(false);
    Menu::OnStart();
    firstFocus = playButton;
    Open();
    ShowPage(Page::Home);
    const auto* settings = GetEntity().GetComponent<Gameplay::SettingsMenu>();
    const std::string name = settings ? settings->GetSettings().playerName : "Player";
    for (const auto id : {hostPlayerName, joinPlayerName}) {
        if (auto* field = Get<Spot::UiInputField>(id)) {
            field->SetText(name);
        }
    }
    if (Gameplay::Session* session = Gameplay::Session::Get()) {
        if (session->GetState() == Gameplay::SessionState::Connecting ||
            session->GetState() == Gameplay::SessionState::Loading) {
            ShowPage(Page::Connecting);
        } else if (session->GetState() == Gameplay::SessionState::Failed) {
            ShowPage(Page::Join);
            SetStatus(session->GetLastError());
        }
    }
}

void MainMenu::Initialize() {
    DiscoverMaps();
    m_previews.resize(maps.size());
    m_previewAttempted.resize(maps.size());
    ConnectButton(playButton, [this] { ShowPage(Page::Play); });
    ConnectButton(hostButton, [this] { ShowPage(Page::Host); });
    ConnectButton(joinButton, [this] { ShowPage(Page::Join); });
    ConnectButton(joinBack, [this] { ShowPage(Page::Home); });
    ConnectButton(joinConfirm, [this] { JoinRoom(); });
    ConnectButton(cancelConnection, [this] {
        if (Gameplay::Session* session = Gameplay::Session::Get()) {
            session->Leave();
        }
        ShowPage(Page::Home);
    });
    ConnectButton(settingsButton, [this] { ShowPage(Page::Settings); });
    ConnectButton(quitButton, [this] { ShowPage(Page::Quit); });
    ConnectButton(backButton, [this] { ShowPage(Page::Home); });
    ConnectButton(settingsBackButton, [this] { ShowPage(Page::Home); });
    ConnectButton(cancelQuitButton, [this] { ShowPage(Page::Home); });
    ConnectButton(confirmQuitButton, [this] {
        if (GetScene().IsLoaded()) {
            Close();
            Spot::Application::Get().QuitGame();
        }
    });
    ConnectButton(startButton, [this] { m_page == Page::Host ? HostRoom() : Play(); });
    for (std::size_t index = 0; index < maps.size(); ++index) {
        ConnectButton(maps[index].button, [this, index] { SelectMap(index); });
    }
    m_previewHook = GetScene().GetOrAddSystem<Spot::RenderHookSystem>().AddRenderHook(
        Spot::RenderStage::BeforeOpaque, [this](const Spot::RenderStageContext& stage) { RenderPreview(stage); });
    SelectMap(0);
}

void MainMenu::ShowPage(Page page) {
    if (m_page == Page::Settings && page != Page::Settings) {
        if (auto* settings = GetEntity().GetComponent<Gameplay::SettingsMenu>()) {
            settings->Save();
        }
    }
    const auto previous = m_page;
    m_page = page;
    for (const auto& [id, visible] :
         {std::pair{homePanel, page == Page::Home}, std::pair{playPanel, page == Page::Play},
          std::pair{settingsPanel, page == Page::Settings}, std::pair{quitPanel, page == Page::Quit},
          std::pair{hostPanel, page == Page::Host}, std::pair{joinPanel, page == Page::Join},
          std::pair{connectingPanel, page == Page::Connecting}}) {
        if (auto* entity = GetScene().GetEntity(id)) {
            entity->SetActive(visible);
        }
    }
    if (auto* panel = GetScene().GetEntity(playPanel)) {
        panel->SetActive(page == Page::Play || page == Page::Host);
    }
    if (auto* label = GetChild<Spot::UiText>(startButton, "Label")) {
        label->text = page == Page::Host ? "Host room" : "Start";
    }
    if (auto* title = Get<Spot::UiText>(playTitle)) {
        title->text = page == Page::Host ? "Host room" : "Play";
    }
    SetStatus({});
    RefreshInput();
    Spot::EntityId focus = playButton;
    if ((page == Page::Play || page == Page::Host) && !maps.empty()) {
        focus = maps[m_selected].button;
    } else if (page == Page::Join) {
        focus = joinAddress;
    } else if (page == Page::Connecting) {
        focus = cancelConnection;
    } else if (page == Page::Settings) {
        focus = settingsBackButton;
    } else if (page == Page::Quit) {
        focus = cancelQuitButton;
    } else if (previous == Page::Settings) {
        focus = settingsButton;
    } else if (previous == Page::Quit) {
        focus = quitButton;
    }
    GetScene().GetOrAddSystem<Spot::UiInputSystem>().Focus(focus);
    if (previous != page)
        PlayClick();
}

void MainMenu::SelectMap(std::size_t index) {
    if (index >= maps.size()) {
        return;
    }
    m_selected = index;
    const auto& map = maps[index];
    if (auto* text = Get<Spot::UiText>(mapTitle))
        text->text = map.name;
    if (auto* text = Get<Spot::UiText>(mapDescription))
        text->text = map.description;
    if (auto* text = Get<Spot::UiText>(mapDetails))
        text->text = map.details;
    if (auto* image = Get<Spot::UiImage>(previewImage)) {
        image->texture = index < m_previews.size() ? m_previews[index] : Spot::AssetRef<Spot::Texture>{};
        image->tint.a = image->texture ? 1.0f : 0.0f;
    }
    SetStatus({});
    if (index < m_previewAttempted.size() && m_previewAttempted[index] && !m_previews[index])
        SetStatus("Preview unavailable. You can still start this map.");
    if (m_page == Page::Play || m_page == Page::Host)
        PlayClick();
}

void MainMenu::SetStatus(std::string text) {
    for (const auto id : {statusLabel, joinStatus, connectingStatus}) {
        if (auto* label = Get<Spot::UiText>(id)) {
            label->text = text;
        }
    }
}

void MainMenu::SavePlayerName(std::string name) {
    if (auto* settings = GetEntity().GetComponent<Gameplay::SettingsMenu>()) {
        settings->SetPlayerName(std::move(name));
    }
}

void MainMenu::HostRoom() {
    Gameplay::Session* session = Gameplay::Session::Get();
    if (!session || maps.empty()) {
        SetStatus("Networking is unavailable.");
        return;
    }
    const auto text = [&](Spot::EntityId id) {
        const auto* field = Get<Spot::UiInputField>(id);
        return field ? field->GetText() : std::string();
    };
    int port = 0;
    int players = 0;
    if (!Spot::ParseConsoleValue(text(hostPort), port) || port < 1 || port > 65535 ||
        !Spot::ParseConsoleValue(text(hostPlayers), players)) {
        SetStatus("Enter a port from 1 to 65535 and a valid player count.");
        return;
    }
    Gameplay::SessionSettings settings;
    settings.port = static_cast<std::uint16_t>(port);
    settings.maxPlayers = players;
    settings.map = maps[m_selected].scene;
    settings.mode = maps[m_selected].defaultMode;
    settings.name = text(hostName);
    settings.playerName = text(hostPlayerName);
    settings.password = text(hostPassword);
    if (session->Host(settings)) {
        SavePlayerName(settings.playerName);
        ShowPage(Page::Connecting);
        SetStatus("Starting your room...");
    } else {
        SetStatus(session->GetLastError());
    }
}

void MainMenu::JoinRoom() {
    Gameplay::Session* session = Gameplay::Session::Get();
    if (!session) {
        SetStatus("Networking is unavailable.");
        return;
    }
    const auto text = [&](Spot::EntityId id) {
        const auto* field = Get<Spot::UiInputField>(id);
        return field ? field->GetText() : std::string();
    };
    const std::string name = text(joinPlayerName);
    if (session->Join(text(joinAddress), name, text(joinPassword))) {
        SavePlayerName(name);
        ShowPage(Page::Connecting);
        SetStatus("Connecting to the room...");
    } else {
        SetStatus(session->GetLastError());
    }
}

void MainMenu::OnUpdate(float deltaTime) {
    Menu::OnUpdate(deltaTime);
    if (m_page != Page::Connecting) {
        return;
    }
    if (Gameplay::Session* session = Gameplay::Session::Get()) {
        if (session->GetState() == Gameplay::SessionState::Failed) {
            ShowPage(Page::Join);
            SetStatus(session->GetLastError());
        } else {
            SetStatus(session->GetState() == Gameplay::SessionState::Connecting ? "Connecting to the room..."
                                                                                : "Loading the room...");
        }
    }
}

void MainMenu::Play() {
    if (m_loading || maps.empty() || !GetScene().IsLoaded()) {
        return;
    }
    if (Spot::Network::IsClient(GetScene())) {
        SetStatus("The server chooses the map.");
        return;
    }
    auto& application = Spot::Application::Get();
    if (auto next = Spot::InstantiateScene(application.GetAssets(), maps[m_selected].scene)) {
        auto* map = Gameplay::MapInfo::Get(*next);
        if (!map || !map->StartMode(map->defaultMode, &application.GetAssets())) {
            SetStatus(map ? map->GetLastError() : "This scene has no map information.");
            return;
        }
        m_loading = true;
        SetStatus("Loading map...");
        Close();
        application.LoadScene(std::move(next));
    } else {
        SetStatus("Could not load this map. Select another map or try again.");
    }
}

void MainMenu::OnRawEvent(const Spot::Event& event, bool& consumed) {
    if (auto* bindings = GetEntity().GetComponent<Gameplay::BindingsMenu>(); bindings && bindings->IsRebinding())
        return;
    const auto* key = event.GetIf<Spot::KeyPressedEvent>();
    const auto* pad = event.GetIf<Spot::GamepadButtonPressedEvent>();
    if ((key && key->key == Spot::Key::Escape) ||
        (pad && (pad->button == Spot::GamepadButton::East || pad->button == Spot::GamepadButton::Start))) {
        if (!key || !key->repeat)
            if (m_page == Page::Connecting) {
                if (Gameplay::Session* session = Gameplay::Session::Get()) {
                    session->Leave();
                }
                ShowPage(Page::Home);
            } else {
                ShowPage(m_page == Page::Home ? Page::Quit : Page::Home);
            }
        consumed = true;
    }
}

void MainMenu::AnimateControls(float blend) {
    for (const auto id :
         {playButton, settingsButton, quitButton, startButton, backButton, settingsBackButton, cancelQuitButton,
          confirmQuitButton, hostButton, joinButton, joinConfirm, joinBack, cancelConnection}) {
        StyleText(id, std::nullopt, blend);
    }
    for (std::size_t index = 0; index < maps.size(); ++index)
        StyleText(maps[index].button, index == m_selected, blend);
    if (auto* image = Get<Spot::UiImage>(shade))
        image->tint.a = std::lerp(image->tint.a, GetShade(m_page), blend);
}

// The controls are plain text: the one focused or under the pointer brightens and steps aside. A short bar in the
// accent marks it, or, among choices such as the maps, only the one selected.
void MainMenu::StyleText(Spot::EntityId control, std::optional<bool> selected, float blend) {
    const auto* widget = Get<Spot::UiWidget>(control);
    if (!widget || !widget->GetEntity().IsActiveInHierarchy()) {
        return;
    }
    const auto state = widget->GetUiState();
    const bool active = state == Spot::UiControlState::Hovered || state == Spot::UiControlState::Pressed ||
                        state == Spot::UiControlState::Focused;
    const bool marked = selected.value_or(active);
    if (auto* label = GetChild<Spot::UiText>(control, "Label"))
        label->color = Mix(label->color, active || marked ? style.text : style.textSecondary, blend);
    if (auto* rect = GetChild<Spot::UiRect>(control, "Label"))
        rect->layout.offset.x = std::lerp(rect->layout.offset.x, active ? 10.0f : 0.0f, blend);
    if (auto* marker = GetChild<Spot::UiImage>(control, "Marker")) {
        auto target = style.accent;
        target.a = marked ? 1.0f : 0.0f;
        marker->tint = Mix(marker->tint, target, blend);
    }
}

void MainMenu::RenderPreview(const Spot::RenderStageContext& stage) {
    if (!IsOpen() || maps.empty())
        return;
    const std::size_t index = m_selected;
    if (!m_previewAttempted[index]) {
        m_previewAttempted[index] = true;
        if (maps[index].preview) {
            m_previews[index] = maps[index].preview;
        }
        auto* assets = Spot::Assets::GetManager();
        auto scene = !m_previews[index] && assets ? Spot::InstantiateScene(*assets, maps[index].scene) : nullptr;
        if (scene) {
            // Never update the preview: input, audio, physics and game scripts must not start in it.
            scene->ForEachComponent<Spot::Canvas>([](Spot::Canvas& widget) { widget.SetEnabled(false); });
            scene->ForEachComponent<Spot::Camera>([](Spot::Camera& camera) { camera.SetEnabled(false); });
            Spot::Camera3D view;
            view.position = maps[index].previewPosition;
            view.LookAt(maps[index].previewTarget);
            auto& entity = scene->CreateEntity("Map Preview Camera");
            entity.GetTransform().SetPosition(view.position);
            entity.GetTransform().SetRotation(view.rotation);
            auto& camera = entity.AddComponent<Spot::Camera>();
            camera.SetFieldOfView(Spot::ToRadians(55.0f));
            camera.SetFarPlane(600.0f);
            if (!m_previewRenderer)
                m_previewRenderer = std::make_unique<Spot::SceneRenderer>(
                    stage.device, Spot::SceneRendererConfig{.targetFormat = Spot::TextureFormat::Rgba8Srgb,
                                                            .sampleCount = 1,
                                                            .shadowMapSize = 1024,
                                                            .shadowCascadeCount = 2,
                                                            .localShadowMapSize = 0});
            auto texture = stage.device.CreateTexture(
                {.width = 1600, .height = 900, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
            if (texture && m_previewRenderer->IsValid()) {
                m_previewRenderer->Render(stage.context, *scene, texture.get());
                m_previews[index] = Spot::AssetRef<Spot::Texture>(std::move(texture));
            }
        }
        if (!m_previews[index] && m_page == Page::Play)
            SetStatus("Preview unavailable. You can still start this map.");
    }
    if (auto* image = Get<Spot::UiImage>(previewImage)) {
        image->texture = m_previews[index];
        image->tint.a = image->texture ? 1.0f : 0.0f;
    }
}

void MainMenu::OnDestroy() {
    Menu::OnDestroy();
    if (m_previewHook != Spot::RenderHookId::Invalid)
        GetScene().GetOrAddSystem<Spot::RenderHookSystem>().RemoveRenderHook(m_previewHook);
}

void MainMenu::Serialize(Spot::Archive& archive) {
    Menu::Serialize(archive);
    archive.Field("homePanel", homePanel);
    archive.Field("playPanel", playPanel);
    archive.Field("settingsPanel", settingsPanel);
    archive.Field("quitPanel", quitPanel);
    archive.Field("playButton", playButton);
    archive.Field("settingsButton", settingsButton);
    archive.Field("quitButton", quitButton);
    archive.Field("startButton", startButton);
    archive.Field("backButton", backButton);
    archive.Field("settingsBackButton", settingsBackButton);
    archive.Field("cancelQuitButton", cancelQuitButton);
    archive.Field("confirmQuitButton", confirmQuitButton);
    archive.Field("previewImage", previewImage);
    archive.Field("shade", shade);
    archive.Field("mapTitle", mapTitle);
    archive.Field("mapDescription", mapDescription);
    archive.Field("mapDetails", mapDetails);
    archive.Field("statusLabel", statusLabel);
    archive.Field("mapButtonTemplate", mapButtonTemplate);
    archive.Field("mapScroll", mapScroll);
    archive.Field("mapDirectory", mapDirectory);
    archive.Field("hostButton", hostButton);
    archive.Field("playTitle", playTitle);
    archive.Field("joinButton", joinButton);
    archive.Field("hostPanel", hostPanel);
    archive.Field("joinPanel", joinPanel);
    archive.Field("connectingPanel", connectingPanel);
    archive.Field("hostName", hostName);
    archive.Field("hostPlayerName", hostPlayerName);
    archive.Field("hostPort", hostPort);
    archive.Field("hostPlayers", hostPlayers);
    archive.Field("hostPassword", hostPassword);
    archive.Field("joinAddress", joinAddress);
    archive.Field("joinPlayerName", joinPlayerName);
    archive.Field("joinPassword", joinPassword);
    archive.Field("joinConfirm", joinConfirm);
    archive.Field("joinBack", joinBack);
    archive.Field("joinStatus", joinStatus);
    archive.Field("connectingStatus", connectingStatus);
    archive.Field("cancelConnection", cancelConnection);
}
