#pragma once

#include "menu_thumbnails.h"
#include "spawnable.h"

#include "gameplay/menus/menu.h"

#include <spot.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

// A compact asset browser with category rows and two thumbnail densities. Left click enters placement;
// middle click spawns immediately while keeping the browser open. Actions share its toolbar.
class SpawnMenu final : public Gameplay::Menu {
public:
    Spot::EntityId grid = Spot::EntityId::Invalid, scroll = Spot::EntityId::Invalid,
                   itemTemplate = Spot::EntityId::Invalid;
    Spot::EntityId propsTab = Spot::EntityId::Invalid, lampsTab = Spot::EntityId::Invalid,
                   weaponsTab = Spot::EntityId::Invalid, npcsTab = Spot::EntityId::Invalid,
                   vehiclesTab = Spot::EntityId::Invalid;
    Spot::EntityId closeButton = Spot::EntityId::Invalid, cleanupButton = Spot::EntityId::Invalid,
                   undoButton = Spot::EntityId::Invalid;
    // The search field, the frame drawn around it, the number of objects shown and the buttons of the two views.
    Spot::EntityId search = Spot::EntityId::Invalid, searchFrame = Spot::EntityId::Invalid,
                   count = Spot::EntityId::Invalid;
    Spot::EntityId gridView = Spot::EntityId::Invalid, compactView = Spot::EntityId::Invalid;
    // Text, and the selected category in titles.
    Spot::AssetRef<Spot::Font> font, titleFont;

    SpawnMenu();

    void SelectCategory(Spawnable::Category category);
    Spawnable::Category GetCategory() const { return m_category; }
    // Shows only the objects whose names hold the text, ignoring case; the categories count them.
    void Search(std::string_view text);
    // Smaller thumbnails, more to a row.
    void SetCompactView(bool compact);
    bool IsCompactView() const { return m_compact; }
    // The object last chosen, which the menu marks; none before the first.
    std::optional<std::size_t> GetChosenSpawn() const { return m_chosen; }
    std::span<const Spot::EntityId> GetSpawnButtons() const { return m_spawnButtons; }

protected:
    void Initialize() override;
    void OnOpen() override;
    void OnRawEvent(const Spot::Event& event, bool& consumed) override;
    void AnimateControls(float blend) override;
    void OnDestroy() override;
    void Serialize(Spot::Archive& archive) override;

private:
    // The tab of each category, in their order.
    std::array<Spot::EntityId, 5> GetTabs() const { return {propsTab, lampsTab, weaponsTab, npcsTab, vehiclesTab}; }
    void Build();
    void ChooseSpawn(std::size_t index);
    void ApplyFilter();
    void RenderBackdrop(const Spot::RenderStageContext& stage);

    std::vector<Spot::EntityId> m_spawnButtons;
    std::vector<std::size_t> m_spawnIndices;
    std::vector<bool> m_availability;
    std::vector<Spawnable::Category> m_categories;
    // The objects' names in lower case, to search.
    std::vector<std::string> m_searchNames;
    std::string m_search;
    std::optional<Spot::Vector2> m_pointer;
    Spawnable::Category m_category = Spawnable::Category::Prop;
    std::optional<std::size_t> m_chosen;
    bool m_compact = false;
    // The grid as authored, for the larger view.
    std::optional<Spot::UiLayoutGroupSettings> m_gridSettings;
    MenuThumbnails m_thumbnails;
    std::unique_ptr<Spot::Tonemapper> m_backdropTonemapper;
    std::unique_ptr<Spot::Renderer2D> m_backdropRenderer;
    std::unique_ptr<Spot::Texture> m_backdropSmall;
    std::shared_ptr<Spot::Texture> m_backdropTexture;
    Spot::RenderHookId m_backdropHook = Spot::RenderHookId::Invalid;
    Spot::RenderHookId m_thumbnailHook = Spot::RenderHookId::Invalid;
};
