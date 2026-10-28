#include "main_menu.h"

#include "gameplay/menus/settings_menu.h"
#include <spot/scene/rendering/render_hook_system.h>
#include <spot/scene/ui/ui_input_system.h>

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
        return 0.66f;
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

void MainMenuMap::Serialize(Spot::Archive& archive) {
    archive.Field("button", button);
    archive.Field("scene", scene, {.assetType = Spot::AssetType::Scene});
    archive.Field("name", name);
    archive.Field("description", description);
    archive.Field("details", details);
    archive.Field("previewPosition", previewPosition);
    archive.Field("previewTarget", previewTarget);
}

void MainMenu::OnStart() {
    Spot::Input::SetCursorLocked(false);
    Menu::OnStart();
    firstFocus = playButton;
    Open();
    ShowPage(Page::Home);
}

void MainMenu::Initialize() {
    m_previews.resize(maps.size());
    m_previewAttempted.resize(maps.size());
    ConnectButton(playButton, [this] { ShowPage(Page::Play); });
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
    ConnectButton(startButton, [this] { Play(); });
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
          std::pair{settingsPanel, page == Page::Settings}, std::pair{quitPanel, page == Page::Quit}}) {
        if (auto* entity = GetScene().GetEntity(id)) {
            entity->SetActive(visible);
        }
    }
    SetStatus({});
    RefreshInput();
    Spot::EntityId focus = playButton;
    if (page == Page::Play && !maps.empty()) {
        focus = maps[m_selected].button;
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
    if (m_page == Page::Play)
        PlayClick();
}

void MainMenu::SetStatus(std::string text) {
    if (auto* label = Get<Spot::UiText>(statusLabel))
        label->text = std::move(text);
}

void MainMenu::Play() {
    if (m_loading || maps.empty() || !GetScene().IsLoaded()) {
        return;
    }
    auto& application = Spot::Application::Get();
    if (auto next = Spot::InstantiateScene(application.GetAssets(), maps[m_selected].scene)) {
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
            ShowPage(m_page == Page::Home ? Page::Quit : Page::Home);
        consumed = true;
    }
}

void MainMenu::AnimateControls(float blend) {
    for (const auto id : {playButton, settingsButton, quitButton, startButton, backButton, settingsBackButton,
                          cancelQuitButton, confirmQuitButton}) {
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
        auto* assets = Spot::Assets::GetManager();
        auto scene = assets ? Spot::InstantiateScene(*assets, maps[index].scene) : nullptr;
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
    archive.Field("maps", maps);
}
