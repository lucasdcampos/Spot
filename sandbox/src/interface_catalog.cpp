#include "runtime_interface.h"

#include "interface_palette.h"
#include "prop_spawner.h"
#include "sound_bank.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace {

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

void RuntimeInterface::BuildSpawnMenu() {
    auto* spawner = GetScene().FindComponent<PropSpawner>();
    auto* templateEntity = GetScene().GetEntity(spawnTemplate);
    auto* parent = GetScene().GetEntity(spawnGrid);
    if (!spawner || !templateEntity || !parent)
        return;
    if (spawner->GetSpawnables().empty())
        spawner->RefreshSpawnables();
    const auto text = templateEntity->SaveToString();
    const auto spawnables = spawner->GetSpawnables();
    for (std::size_t index = 0; index < spawnables.size(); ++index) {
        auto created = GetScene().Instantiate(text, {.parent = parent});
        if (created.empty())
            continue;
        auto& entity = *created.front();
        entity.SetActive(true);
        entity.SetName(spawnables[index].name);
        m_spawnButtons.push_back(entity.GetId());
        m_categories.push_back(spawnables[index].category);
        m_searchNames.push_back(Lower(spawnables[index].name));
        if (auto* caption = entity.FindChild("Label")) {
            if (auto* label = caption->GetComponent<Spot::UiText>()) {
                label->text = spawnables[index].name;
                label->font = font;
            }
        }
        if (auto* image = entity.FindChild("Thumbnail")) {
            // Hidden until it is drawn, rather than a blank square.
            if (auto* picture = image->GetComponent<Spot::UiImage>())
                picture->tint.a = 0.0f;
            m_thumbnails.Add(image->GetId(), spawnables[index].prefab);
        }
        ConnectButton(entity.GetId(), [this, index] { ChooseSpawn(index); });
    }
    SetCompactView(false);
    SelectCategory(Spawnable::Category::Prop);
}
void RuntimeInterface::ChooseSpawn(std::size_t index) {
    // Closed first, so the cursor is the game's again as the preview follows the aim.
    m_chosen = index;
    CloseMenus();
    if (auto* spawner = GetScene().FindComponent<PropSpawner>())
        spawner->BeginPlacement(index);
    SoundBank::PlayClick(GetScene());
}
void RuntimeInterface::SelectCategory(Spawnable::Category category) {
    m_category = category;
    ApplySpawnFilter();
}
void RuntimeInterface::Search(std::string_view text) {
    if (auto* field = Get<Spot::UiInputField>(spawnSearch); field && field->GetText() != text)
        field->SetText(std::string(text));
    m_search = Lower(text);
    ApplySpawnFilter();
}
void RuntimeInterface::SetCompactView(bool compact) {
    m_compact = compact;
    auto* group = Get<Spot::UiLayoutGroup>(spawnGrid);
    if (!group)
        return;
    if (!m_gridSettings)
        m_gridSettings = group->settings;
    group->settings = *m_gridSettings;
    if (compact) {
        group->settings.cellSize = CompactCell;
        group->settings.columns = CompactColumns;
    }
    const Spot::Vector2 cell = group->settings.cellSize;
    for (const auto id : m_spawnButtons) {
        if (auto* rect = Get<Spot::UiRect>(id))
            rect->layout.sizeDelta = cell;
        if (auto* thumbnail = GetChild<Spot::UiRect>(id, "Thumbnail")) {
            const float side = cell.x - ThumbnailInset * 2.0f;
            thumbnail->layout.sizeDelta = {side, side};
        }
        if (auto* label = GetChild<Spot::UiText>(id, "Label"))
            label->fontSize = compact ? 11.0f : 12.0f;
    }
    ApplySpawnFilter();
}
void RuntimeInterface::ApplySpawnFilter() {
    namespace Palette = InterfacePalette;
    // What matches the search in each category, which the tabs count, and of those the ones of the category shown.
    std::array<std::size_t, 3> found{};
    std::size_t shown = 0;
    for (std::size_t index = 0; index < m_spawnButtons.size(); ++index) {
        const bool match = m_search.empty() || m_searchNames[index].find(m_search) != std::string::npos;
        found[static_cast<std::size_t>(m_categories[index])] += match ? 1 : 0;
        const bool active = match && m_categories[index] == m_category;
        if (auto* entity = GetScene().GetEntity(m_spawnButtons[index]))
            entity->SetActive(active);
        shown += active ? 1 : 0;
    }
    // The tab shown is raised, marked in the accent and set in the title font.
    const std::pair<Spot::EntityId, Spawnable::Category> tabs[] = {{propsTab, Spawnable::Category::Prop},
                                                                   {lampsTab, Spawnable::Category::Lamp},
                                                                   {weaponsTab, Spawnable::Category::Weapon}};
    for (const auto& [id, category] : tabs) {
        auto* tab = Get<Spot::UiButton>(id);
        if (!tab)
            continue;
        const bool selected = category == m_category;
        tab->style.normal = Palette::WithAlpha(Palette::Surface, selected ? 1.0f : 0.0f);
        if (auto* marker = tab->GetEntity().FindChild("Marker"))
            marker->SetActive(selected);
        if (auto* label = GetChild<Spot::UiText>(id, "Label"))
            label->font = selected && titleFont ? titleFont : font;
        if (auto* count = GetChild<Spot::UiText>(id, "Count")) {
            count->text = std::to_string(found[static_cast<std::size_t>(category)]);
            count->color = selected ? Palette::TextSecondary : Palette::TextMuted;
        }
    }
    if (auto* count = Get<Spot::UiText>(spawnCount))
        count->text = std::format("{} {}", shown, shown == 1 ? "OBJECT" : "OBJECTS");
    if (auto* scroll = Get<Spot::UiScrollView>(spawnScroll)) {
        if (const auto* group = Get<Spot::UiLayoutGroup>(spawnGrid)) {
            std::vector<Spot::Vector2> sizes(shown, group->settings.cellSize);
            scroll->contentSize.y = Spot::MeasureUiLayoutGroup(group->settings, sizes).y;
        }
        scroll->SetOffset({});
    }
    RefreshInput();
}
