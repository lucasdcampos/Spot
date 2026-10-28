#pragma once

#include "gameplay/menus/menu.h"

#include <spot.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct MainMenuMap {
    Spot::EntityId button = Spot::EntityId::Invalid;
    Spot::AssetId scene;
    std::string name, description, details;
    Spot::Vector3 previewPosition, previewTarget;

    void Serialize(Spot::Archive& archive);
};

// The startup interface, authored in main_menu.scene: text-only pages over a full-screen view of the selected map,
// rendered from its scene without starting gameplay.
class MainMenu final : public Gameplay::Menu {
public:
    enum class Page { Home, Play, Settings, Quit };

    Spot::EntityId homePanel = Spot::EntityId::Invalid, playPanel = Spot::EntityId::Invalid,
                   settingsPanel = Spot::EntityId::Invalid, quitPanel = Spot::EntityId::Invalid;
    Spot::EntityId playButton = Spot::EntityId::Invalid, settingsButton = Spot::EntityId::Invalid,
                   quitButton = Spot::EntityId::Invalid, startButton = Spot::EntityId::Invalid;
    Spot::EntityId backButton = Spot::EntityId::Invalid, settingsBackButton = Spot::EntityId::Invalid,
                   cancelQuitButton = Spot::EntityId::Invalid, confirmQuitButton = Spot::EntityId::Invalid;
    Spot::EntityId previewImage = Spot::EntityId::Invalid, shade = Spot::EntityId::Invalid,
                   mapTitle = Spot::EntityId::Invalid, mapDescription = Spot::EntityId::Invalid,
                   mapDetails = Spot::EntityId::Invalid, statusLabel = Spot::EntityId::Invalid;
    std::vector<MainMenuMap> maps;

    Page GetPage() const { return m_page; }
    std::size_t GetSelectedMap() const { return m_selected; }
    void ShowPage(Page page);
    void SelectMap(std::size_t index);

protected:
    void OnStart() override;
    void Initialize() override;
    void AnimateControls(float blend) override;
    void OnRawEvent(const Spot::Event& event, bool& consumed) override;
    void OnDestroy() override;
    void Serialize(Spot::Archive& archive) override;

private:
    void Play();
    void StyleText(Spot::EntityId control, std::optional<bool> selected, float blend);
    void RenderPreview(const Spot::RenderStageContext& stage);
    void SetStatus(std::string text);

    Page m_page = Page::Home;
    std::size_t m_selected = 0;
    std::vector<Spot::AssetRef<Spot::Texture>> m_previews;
    std::vector<bool> m_previewAttempted;
    std::unique_ptr<Spot::SceneRenderer> m_previewRenderer;
    Spot::RenderHookId m_previewHook = Spot::RenderHookId::Invalid;
    bool m_loading = false;
};
