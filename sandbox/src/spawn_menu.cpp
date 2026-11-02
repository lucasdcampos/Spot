#include "spawn_menu.h"
#include "gameplay/core/players.h"
#include <spot/scene/rendering/post_processing.h>
#include <spot/scene/rendering/render_hook_system.h>
#include <spot/scene/ui/ui_input_system.h>

#include "prop_spawner.h"
#include "undo_list.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>

SPOT_COMPONENT(SpawnMenu);

namespace {

std::string Lower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

// Both densities keep the thumbnail dominant and leave one line for the name.
constexpr Spot::Vector2 CompactCell{78.0f, 100.0f};
constexpr int CompactColumns = 12;
// Keep the preview nearly flush with its cell.
constexpr float ThumbnailInset = 2.0f;

} // namespace

SpawnMenu::SpawnMenu() {
    toggleAction = "Spawn Menu";
}

void SpawnMenu::Initialize() {
    Build();
    ConnectButton(propsTab, [this] { SelectCategory(Spawnable::Category::Prop); });
    ConnectButton(lampsTab, [this] { SelectCategory(Spawnable::Category::Lamp); });
    ConnectButton(weaponsTab, [this] { SelectCategory(Spawnable::Category::Weapon); });
    ConnectButton(npcsTab, [this] { SelectCategory(Spawnable::Category::Npc); });
    ConnectButton(vehiclesTab, [this] { SelectCategory(Spawnable::Category::Vehicle); });
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
    m_backdropHook = GetScene().GetOrAddSystem<Spot::RenderHookSystem>().AddRenderHook(
        Spot::RenderStage::BeforePostProcessing, [this](const Spot::RenderStageContext& stage) {
            if (IsOpen()) {
                RenderBackdrop(stage);
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
    Build();
    ApplyFilter();
    // Back in the menu, the prop being placed is put away; the menu still marks it.
    if (auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(
            GetScene(), static_cast<std::size_t>(std::max(playerIndex, 0)))) {
        spawner->CancelPlacement();
    }
    GetScene().GetOrAddSystem<Spot::UiInputSystem>().Focus(GetTabs()[static_cast<std::size_t>(m_category)]);
}

void SpawnMenu::OnDestroy() {
    Menu::OnDestroy();
    if (m_backdropHook != Spot::RenderHookId::Invalid) {
        GetScene().GetOrAddSystem<Spot::RenderHookSystem>().RemoveRenderHook(m_backdropHook);
    }
    if (m_thumbnailHook != Spot::RenderHookId::Invalid) {
        GetScene().GetOrAddSystem<Spot::RenderHookSystem>().RemoveRenderHook(m_thumbnailHook);
    }
}

void SpawnMenu::Build() {
    if (!m_spawnButtons.empty()) {
        return;
    }
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
        m_spawnIndices.push_back(index);
        m_availability.push_back(spawner->IsAvailable(index));
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

void SpawnMenu::OnRawEvent(const Spot::Event& event, bool& consumed) {
    if (const auto* move = event.GetIf<Spot::MouseMovedEvent>()) {
        m_pointer = move->position;
    }
    const auto* mouse = event.GetIf<Spot::MouseButtonPressedEvent>();
    if (!IsOpen() || !mouse || mouse->button != Spot::MouseButton::Middle) {
        return;
    }
    if (!m_pointer) {
        return;
    }
    const auto viewSize = GetScene().GetOrAddSystem<Spot::UiSystem>().GetViewSize();
    const Spot::Vector2 point{m_pointer->x, viewSize.y - m_pointer->y};
    for (std::size_t index = 0; index < m_spawnButtons.size(); ++index) {
        const auto* widget = Get<Spot::UiWidget>(m_spawnButtons[index]);
        const auto* layout = widget ? widget->GetLayout() : nullptr;
        if (!widget || !widget->IsActiveAndEnabled() || !widget->IsInteractable() || !layout ||
            !layout->bounds.Contains(point) || !layout->clip.Contains(point)) {
            continue;
        }
        if (auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(
                GetScene(), static_cast<std::size_t>(std::max(playerIndex, 0)))) {
            m_chosen = m_spawnIndices[index];
            spawner->Spawn(*m_chosen);
            PlayClick();
        }
        consumed = true;
        return;
    }
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
            label->fontSize = 12.0f;
        }
    }
    ApplyFilter();
}

void SpawnMenu::ApplyFilter() {
    // What matches the search in each category, which the tabs count, and of those the ones of the category shown.
    auto* spawner =
        Gameplay::Players::FindComponent<PropSpawner>(GetScene(), static_cast<std::size_t>(std::max(playerIndex, 0)));
    std::array<std::size_t, 5> found{};
    std::size_t shown = 0;
    for (std::size_t index = 0; index < m_spawnButtons.size(); ++index) {
        m_availability[index] = spawner && spawner->IsAvailable(m_spawnIndices[index]);
        const bool match =
            m_availability[index] && (m_search.empty() || m_searchNames[index].find(m_search) != std::string::npos);
        found[static_cast<std::size_t>(m_categories[index])] += match ? 1 : 0;
        const bool active = match && m_categories[index] == m_category;
        if (Spot::Entity* entity = GetScene().GetEntity(m_spawnButtons[index])) {
            entity->SetActive(active);
        }
        shown += active ? 1 : 0;
    }
    // Categories behave like compact explorer rows.
    const auto tabs = GetTabs();
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        const Spot::EntityId id = tabs[index];
        const auto category = static_cast<Spawnable::Category>(index);
        auto* tab = Get<Spot::UiButton>(id);
        if (!tab) {
            continue;
        }
        const bool selected = category == m_category;
        tab->style.normal = Spot::Color{1, 1, 1, selected ? 0.06f : 0.0f};
        if (Spot::Entity* marker = tab->GetEntity().FindChild("Marker")) {
            marker->SetActive(false);
        }
        if (auto* label = GetChild<Spot::UiText>(id, "Label")) {
            label->font = font;
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
    if (const auto* group = Get<Spot::UiLayoutGroup>(grid)) {
        std::vector<Spot::Vector2> sizes(shown, group->settings.cellSize);
        const float height = std::clamp(Spot::MeasureUiLayoutGroup(group->settings, sizes).y + 42.0f, 200.0f, 480.0f);
        if (auto* root = GetScene().GetEntity(canvas)) {
            for (const auto name : {"Spawn Panel", "Spawn Backdrop"}) {
                if (auto* panel = root->FindChild(name)) {
                    if (auto* rect = panel->GetComponent<Spot::UiRect>()) {
                        rect->layout.sizeDelta.y = height;
                    }
                }
            }
        }
    }
    RefreshInput();
}

void SpawnMenu::AnimateControls(float blend) {
    // A client's character can arrive after its menus, including while this one is open.
    if (IsOpen()) {
        Build();
        if (auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(
                GetScene(), static_cast<std::size_t>(std::max(playerIndex, 0)))) {
            for (std::size_t i = 0; i < m_spawnIndices.size(); ++i) {
                if (m_availability[i] != spawner->IsAvailable(m_spawnIndices[i])) {
                    ApplyFilter();
                    break;
                }
            }
        }
    }
    bool hovering = false;
    for (std::size_t index = 0; index < m_spawnButtons.size(); ++index) {
        if (const auto* widget = Get<Spot::UiWidget>(m_spawnButtons[index]); widget && widget->IsActiveAndEnabled()) {
            hovering |= widget->GetUiState() == Spot::UiControlState::Hovered;
        }
        const bool selected = m_chosen == m_spawnIndices[index];
        if (auto* button = Get<Spot::UiButton>(m_spawnButtons[index])) {
            button->style.normal = {1, 1, 1, selected ? 0.10f : 0.035f};
        }
        StyleControl(m_spawnButtons[index], false, style.textSecondary, blend);
    }
    if (auto* root = GetScene().GetEntity(canvas)) {
        if (auto* panel = root->FindChild("Spawn Panel")) {
            if (auto* hint = panel->FindChild("Spawn Hint")) {
                hint->SetActive(hovering);
            }
        }
    }
    const auto tabs = GetTabs();
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        StyleControl(tabs[index], m_category == static_cast<Spawnable::Category>(index), style.textSecondary, blend);
    }
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

void SpawnMenu::RenderBackdrop(const Spot::RenderStageContext& stage) {
    auto* root = GetScene().GetEntity(canvas);
    auto* background = root ? root->FindChild("Spawn Backdrop") : nullptr;
    auto* image = background ? background->GetComponent<Spot::UiImage>() : nullptr;
    if (!image || !stage.sceneColor) {
        return;
    }
    auto& ui = GetScene().GetOrAddSystem<Spot::UiSystem>();
    ui.Layout(stage.viewSize);
    const auto* layout = ui.Find(background->GetId());
    if (!layout) {
        return;
    }
    const auto width = static_cast<std::uint32_t>(std::max(1.0f, stage.viewSize.x / 12.0f));
    const auto height = static_cast<std::uint32_t>(std::max(1.0f, stage.viewSize.y / 12.0f));
    if (!m_backdropTonemapper) {
        m_backdropTonemapper = Spot::Tonemapper::Create(stage.device, Spot::TextureFormat::Rgba8);
        m_backdropRenderer = std::make_unique<Spot::Renderer2D>(
            stage.device, Spot::Renderer2DConfig{.targetFormat = Spot::TextureFormat::Rgba8});
    }
    if (!m_backdropTonemapper || !m_backdropRenderer->IsValid()) {
        return;
    }
    if (!m_backdropSmall || m_backdropSmall->GetWidth() != width || m_backdropSmall->GetHeight() != height) {
        m_backdropSmall = stage.device.CreateTexture(
            {.width = width, .height = height, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    }
    const auto size = layout->bounds.GetSize();
    const auto panelWidth = static_cast<std::uint32_t>(std::max(1.0f, size.x / 4.0f));
    const auto panelHeight = static_cast<std::uint32_t>(std::max(1.0f, size.y / 4.0f));
    if (!m_backdropTexture || m_backdropTexture->GetWidth() != panelWidth ||
        m_backdropTexture->GetHeight() != panelHeight) {
        m_backdropTexture = stage.device.CreateTexture(
            {.width = panelWidth, .height = panelHeight, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    }
    if (!m_backdropSmall || !m_backdropTexture) {
        return;
    }
    // Downsampling and bilinear reconstruction soften the scene without blurring labels or thumbnails.
    {
        auto pass = stage.context.BeginRenderPass({.target = m_backdropSmall.get()});
        const std::array scenes{&GetScene()};
        const auto* look = Spot::PostProcessing::Find(scenes);
        auto tonemapping = look ? look->GetTonemapping(scenes) : Spot::TonemapSettings{};
        tonemapping.dither = false;
        m_backdropTonemapper->Draw(pass, *stage.sceneColor, tonemapping,
                                   look ? look->GetGrading() : Spot::GradingSettings{});
    }
    {
        auto pass = stage.context.BeginRenderPass({.target = m_backdropTexture.get()});
        const Spot::Vector2 targetSize{static_cast<float>(panelWidth), static_cast<float>(panelHeight)};
        m_backdropRenderer->Begin(pass, Spot::UiSystem::ScreenCamera(targetSize), targetSize);
        const Spot::TextureRegion region{
            {layout->bounds.min.x / stage.viewSize.x, 1.0f - layout->bounds.max.y / stage.viewSize.y},
            {layout->bounds.max.x / stage.viewSize.x, 1.0f - layout->bounds.min.y / stage.viewSize.y}};
        m_backdropRenderer->DrawQuad(targetSize * 0.5f, targetSize, *m_backdropSmall, Spot::Color::White, region);
        m_backdropRenderer->End();
    }
    image->texture = Spot::AssetRef<Spot::Texture>(m_backdropTexture);
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
    archive.Field("npcsTab", npcsTab);
    archive.Field("vehiclesTab", vehiclesTab);
    archive.Field("closeButton", closeButton);
    archive.Field("cleanupButton", cleanupButton);
    archive.Field("undoButton", undoButton);
    archive.Field("search", search);
    archive.Field("searchFrame", searchFrame);
    archive.Field("count", count);
    archive.Field("gridView", gridView);
    archive.Field("compactView", compactView);
}
