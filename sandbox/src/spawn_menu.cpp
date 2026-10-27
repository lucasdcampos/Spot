#include "spawn_menu.h"
#include "gameplay/core/players.h"
#include <spot/scene/rendering/render_hook_system.h>
#include <spot/scene/ui/ui_input_system.h>

#include "interface_palette.h"
#include "prop_spawner.h"
#include "undo_list.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>

SPOT_COMPONENT(SpawnMenu);

namespace {

namespace Palette = InterfacePalette;

std::string Lower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

// The compact view's cards and how many fit a row of the grid.
constexpr Spot::Vector2 CompactCell{104.0f, 124.0f};
constexpr int CompactColumns = 8;
// The thumbnail's inset from the card's sides and top, in canvas pixels.
constexpr float ThumbnailInset = 6.0f;

} // namespace

SpawnMenu::SpawnMenu() {
    toggleAction = "Spawn Menu";
}

void SpawnMenu::Initialize() {
    Build();
    ConnectButton(propsTab, [this] { SelectCategory(Spawnable::Category::Prop); });
    ConnectButton(lampsTab, [this] { SelectCategory(Spawnable::Category::Lamp); });
    ConnectButton(weaponsTab, [this] { SelectCategory(Spawnable::Category::Weapon); });
    ConnectButton(gridView, [this] { SetCompactView(false); });
    ConnectButton(compactView, [this] { SetCompactView(true); });
    if (auto* field = Get<Spot::UiInputField>(search)) {
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
    ConnectButton(closeButton, [this] { Close(); });
    ConnectButton(cleanupButton, [this] {
        if (auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(
                GetScene(), static_cast<std::size_t>(std::max(playerIndex, 0)))) {
            spawner->Cleanup();
        }
    });
    ConnectButton(undoButton, [this] {
        if (auto* undo = Gameplay::Players::FindComponent<UndoList>(
                GetScene(), static_cast<std::size_t>(std::max(playerIndex, 0)))) {
            undo->Undo();
        }
    });
    m_thumbnailHook = GetScene().GetOrAddSystem<Spot::RenderHookSystem>().AddRenderHook(
        Spot::RenderStage::BeforeOpaque, [this](const Spot::RenderStageContext& stage) {
            if (IsOpen()) {
                m_thumbnails.Render(stage, GetScene());
            }
        });
}

void SpawnMenu::OnOpen() {
    // Back in the menu, the prop being placed is put away; the menu still marks it.
    if (auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(
            GetScene(), static_cast<std::size_t>(std::max(playerIndex, 0)))) {
        spawner->CancelPlacement();
    }
    GetScene().GetOrAddSystem<Spot::UiInputSystem>().Focus(m_category == Spawnable::Category::Prop   ? propsTab
                                                           : m_category == Spawnable::Category::Lamp ? lampsTab
                                                                                                     : weaponsTab);
}

void SpawnMenu::OnDestroy() {
    Menu::OnDestroy();
    if (m_thumbnailHook != Spot::RenderHookId::Invalid) {
        GetScene().GetOrAddSystem<Spot::RenderHookSystem>().RemoveRenderHook(m_thumbnailHook);
    }
}

void SpawnMenu::Build() {
    auto* spawner =
        Gameplay::Players::FindComponent<PropSpawner>(GetScene(), static_cast<std::size_t>(std::max(playerIndex, 0)));
    Spot::Entity* templateEntity = GetScene().GetEntity(itemTemplate);
    Spot::Entity* parent = GetScene().GetEntity(grid);
    if (!spawner || !templateEntity || !parent) {
        return;
    }
    if (spawner->GetSpawnables().empty()) {
        spawner->RefreshSpawnables();
    }
    const auto text = templateEntity->SaveToString();
    const auto spawnables = spawner->GetSpawnables();
    for (std::size_t index = 0; index < spawnables.size(); ++index) {
        auto created = GetScene().Instantiate(text, {.parent = parent});
        if (created.empty()) {
            continue;
        }
        Spot::Entity& entity = *created.front();
        entity.SetActive(true);
        entity.SetName(spawnables[index].name);
        m_spawnButtons.push_back(entity.GetId());
        m_categories.push_back(spawnables[index].category);
        m_searchNames.push_back(Lower(spawnables[index].name));
        if (Spot::Entity* caption = entity.FindChild("Label")) {
            if (auto* label = caption->GetComponent<Spot::UiText>()) {
                label->text = spawnables[index].name;
                label->font = font;
            }
        }
        if (Spot::Entity* image = entity.FindChild("Thumbnail")) {
            // Hidden until it is drawn, rather than a blank square.
            if (auto* picture = image->GetComponent<Spot::UiImage>()) {
                picture->tint.a = 0.0f;
            }
            m_thumbnails.Add(image->GetId(), spawnables[index].prefab);
        }
        ConnectButton(entity.GetId(), [this, index] { ChooseSpawn(index); });
    }
    SetCompactView(false);
    SelectCategory(Spawnable::Category::Prop);
}

void SpawnMenu::ChooseSpawn(std::size_t index) {
    // Closed first, so the cursor is the game's again as the preview follows the aim.
    m_chosen = index;
    Close();
    if (auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(
            GetScene(), static_cast<std::size_t>(std::max(playerIndex, 0)))) {
        spawner->BeginPlacement(index);
    }
    PlayClick();
}

void SpawnMenu::SelectCategory(Spawnable::Category category) {
    m_category = category;
    ApplyFilter();
}

void SpawnMenu::Search(std::string_view text) {
    if (auto* field = Get<Spot::UiInputField>(search); field && field->GetText() != text) {
        field->SetText(std::string(text));
    }
    m_search = Lower(text);
    ApplyFilter();
}

void SpawnMenu::SetCompactView(bool compact) {
    m_compact = compact;
    auto* group = Get<Spot::UiLayoutGroup>(grid);
    if (!group) {
        return;
    }
    if (!m_gridSettings) {
        m_gridSettings = group->settings;
    }
    group->settings = *m_gridSettings;
    if (compact) {
        group->settings.cellSize = CompactCell;
        group->settings.columns = CompactColumns;
    }
    const Spot::Vector2 cell = group->settings.cellSize;
    for (const Spot::EntityId id : m_spawnButtons) {
        if (auto* rect = Get<Spot::UiRect>(id)) {
            rect->layout.sizeDelta = cell;
        }
        if (auto* thumbnail = GetChild<Spot::UiRect>(id, "Thumbnail")) {
            const float side = cell.x - ThumbnailInset * 2.0f;
            thumbnail->layout.sizeDelta = {side, side};
        }
        if (auto* label = GetChild<Spot::UiText>(id, "Label")) {
            label->fontSize = compact ? 11.0f : 12.0f;
        }
    }
    ApplyFilter();
}

void SpawnMenu::ApplyFilter() {
    // What matches the search in each category, which the tabs count, and of those the ones of the category shown.
    std::array<std::size_t, 3> found{};
    std::size_t shown = 0;
    for (std::size_t index = 0; index < m_spawnButtons.size(); ++index) {
        const bool match = m_search.empty() || m_searchNames[index].find(m_search) != std::string::npos;
        found[static_cast<std::size_t>(m_categories[index])] += match ? 1 : 0;
        const bool active = match && m_categories[index] == m_category;
        if (Spot::Entity* entity = GetScene().GetEntity(m_spawnButtons[index])) {
            entity->SetActive(active);
        }
        shown += active ? 1 : 0;
    }
    // The tab shown is raised, marked in the accent and set in the title font.
    const std::pair<Spot::EntityId, Spawnable::Category> tabs[] = {{propsTab, Spawnable::Category::Prop},
                                                                   {lampsTab, Spawnable::Category::Lamp},
                                                                   {weaponsTab, Spawnable::Category::Weapon}};
    for (const auto& [id, category] : tabs) {
        auto* tab = Get<Spot::UiButton>(id);
        if (!tab) {
            continue;
        }
        const bool selected = category == m_category;
        tab->style.normal = Palette::WithAlpha(Palette::Surface, selected ? 1.0f : 0.0f);
        if (Spot::Entity* marker = tab->GetEntity().FindChild("Marker")) {
            marker->SetActive(selected);
        }
        if (auto* label = GetChild<Spot::UiText>(id, "Label")) {
            label->font = selected && titleFont ? titleFont : font;
        }
        if (auto* number = GetChild<Spot::UiText>(id, "Count")) {
            number->text = std::to_string(found[static_cast<std::size_t>(category)]);
            number->color = selected ? style.textSecondary : style.textMuted;
        }
    }
    if (auto* text = Get<Spot::UiText>(count)) {
        text->text = std::format("{} {}", shown, shown == 1 ? "OBJECT" : "OBJECTS");
    }
    if (auto* view = Get<Spot::UiScrollView>(scroll)) {
        if (const auto* group = Get<Spot::UiLayoutGroup>(grid)) {
            std::vector<Spot::Vector2> sizes(shown, group->settings.cellSize);
            view->contentSize.y = Spot::MeasureUiLayoutGroup(group->settings, sizes).y;
        }
        view->SetOffset({});
    }
    RefreshInput();
}

void SpawnMenu::AnimateControls(float blend) {
    for (std::size_t index = 0; index < m_spawnButtons.size(); ++index) {
        StyleControl(m_spawnButtons[index], m_chosen == index, style.text, blend);
    }
    StyleControl(propsTab, m_category == Spawnable::Category::Prop, style.textSecondary, blend);
    StyleControl(lampsTab, m_category == Spawnable::Category::Lamp, style.textSecondary, blend);
    StyleControl(weaponsTab, m_category == Spawnable::Category::Weapon, style.textSecondary, blend);
    StyleControl(gridView, !m_compact, style.textSecondary, blend);
    StyleControl(compactView, m_compact, style.textSecondary, blend);
    for (const Spot::EntityId id : {undoButton, cleanupButton, closeButton}) {
        StyleControl(id, false, style.textSecondary, blend);
    }
    const auto* field = Get<Spot::UiWidget>(search);
    const bool typing = field && field->GetUiState() == Spot::UiControlState::Focused;
    const bool hot = field && field->GetUiState() == Spot::UiControlState::Hovered;
    const auto approach = [blend](Spot::Color& color, Spot::Color target) {
        color = {std::lerp(color.r, target.r, blend), std::lerp(color.g, target.g, blend),
                 std::lerp(color.b, target.b, blend), std::lerp(color.a, target.a, blend)};
    };
    if (auto* outline = GetChild<Spot::UiImage>(searchFrame, "Outline")) {
        approach(outline->tint, typing ? style.lineFocused : hot ? style.lineHovered : style.line);
    }
    if (auto* icon = GetChild<Spot::UiImage>(searchFrame, "Icon")) {
        approach(icon->tint, typing ? style.text : style.textMuted);
    }
}

void SpawnMenu::Serialize(Spot::Archive& archive) {
    Menu::Serialize(archive);
    archive.Field("font", font);
    archive.Field("titleFont", titleFont, {.tooltip = "For the selected category"});
    archive.Field("grid", grid);
    archive.Field("scroll", scroll);
    archive.Field("itemTemplate", itemTemplate);
    archive.Field("propsTab", propsTab);
    archive.Field("lampsTab", lampsTab);
    archive.Field("weaponsTab", weaponsTab);
    archive.Field("closeButton", closeButton);
    archive.Field("cleanupButton", cleanupButton);
    archive.Field("undoButton", undoButton);
    archive.Field("search", search);
    archive.Field("searchFrame", searchFrame);
    archive.Field("count", count);
    archive.Field("gridView", gridView);
    archive.Field("compactView", compactView);
}
