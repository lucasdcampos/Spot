#include "runtime_interface.h"

#include "prop_spawner.h"

#include <algorithm>

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
        ConnectButton(entity.GetId(), [this, index] {
            if (auto* current = GetScene().FindComponent<PropSpawner>()) {
                current->Spawn(index);
                RefreshInput();
            }
        });
    }
    SelectCategory(Spawnable::Category::Prop);
}
void RuntimeInterface::SelectCategory(Spawnable::Category category) {
    // The tab of what is shown is lit with the accent, with dark ink.
    const std::pair<Spot::EntityId, Spawnable::Category> tabs[] = {{propsTab, Spawnable::Category::Prop},
                                                                   {lampsTab, Spawnable::Category::Lamp},
                                                                   {weaponsTab, Spawnable::Category::Weapon}};
    for (const auto& [id, tabCategory] : tabs) {
        auto* tab = Get<Spot::UiButton>(id);
        if (!tab)
            continue;
        if (!m_tabStyle)
            m_tabStyle = tab->style;
        const bool selected = tabCategory == category;
        tab->style = *m_tabStyle;
        if (selected) {
            const Spot::Color accent = m_tabStyle->accent;
            tab->style.normal = tab->style.hovered = tab->style.focused = accent;
            tab->style.pressed = {accent.r * 0.8f, accent.g * 0.8f, accent.b * 0.8f, accent.a};
        }
        if (auto* caption = tab->GetEntity().FindChild("Label")) {
            if (auto* label = caption->GetComponent<Spot::UiText>())
                label->color =
                    selected ? Spot::Color{0.07f, 0.06f, 0.04f, 1.0f} : Spot::Color{0.95f, 0.97f, 1.0f, 1.0f};
        }
    }
    std::size_t count = 0;
    for (std::size_t index = 0; index < m_spawnButtons.size(); ++index) {
        const bool active = m_categories[index] == category;
        if (auto* entity = GetScene().GetEntity(m_spawnButtons[index]))
            entity->SetActive(active);
        if (active)
            ++count;
    }
    if (auto* scroll = Get<Spot::UiScrollView>(spawnScroll)) {
        if (const auto* group = Get<Spot::UiLayoutGroup>(spawnGrid)) {
            std::vector<Spot::Vector2> sizes(count, group->settings.cellSize);
            scroll->contentSize.y = Spot::MeasureUiLayoutGroup(group->settings, sizes).y;
        }
        scroll->SetOffset({});
    }
    RefreshInput();
}
