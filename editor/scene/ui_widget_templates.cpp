#include "scene/ui_widget_templates.h"

#include "scene/scene_commands.h"

#include <algorithm>
#include <memory>

namespace Spot::Editor {
namespace {
std::string MakeUiTemplate(EntityTemplate kind, bool wrap, AssetId font) {
    Scene scene;
    Entity* root = &scene.CreateEntity(wrap || kind == EntityTemplate::Canvas ? "Canvas" : "UI");
    if (wrap || kind == EntityTemplate::Canvas)
        root->AddComponent<Canvas>();
    if (kind == EntityTemplate::Canvas)
        return scene.SaveToString();
    const auto templates = GetEntityTemplates();
    const auto found = std::ranges::find(templates, kind, &EntityTemplateInfo::kind);
    Entity& element = wrap ? root->CreateChild(found->name) : *root;
    element.SetName(found->name);
    auto& rect = element.AddComponent<UiRect>();
    rect.layout.sizeDelta = {160, 40};
    const AssetRef<Font> fontRef{font, nullptr};
    switch (kind) {
    case EntityTemplate::UiImage:
        element.AddComponent<UiImage>();
        break;
    case EntityTemplate::UiText: {
        auto& text = element.AddComponent<UiText>();
        text.font = fontRef;
        text.text = "Text";
        rect.layout.sizeDelta = {200, 40};
        break;
    }
    case EntityTemplate::UiButton:
        element.AddComponent<UiButton>();
        break;
    case EntityTemplate::UiToggle:
        element.AddComponent<UiToggle>();
        rect.layout.sizeDelta = {24, 24};
        break;
    case EntityTemplate::UiSlider:
        element.AddComponent<UiSlider>();
        rect.layout.sizeDelta = {220, 24};
        break;
    case EntityTemplate::UiInputField:
        element.AddComponent<UiInputField>().font = fontRef;
        rect.layout.sizeDelta = {220, 36};
        break;
    case EntityTemplate::UiScrollView: {
        element.AddComponent<UiScrollView>();
        rect.layout.sizeDelta = {400, 300};
        auto& content = element.CreateChild("Content").AddComponent<UiRect>();
        content.layout.anchorMin = {};
        content.layout.anchorMax = {1, 1};
        content.layout.sizeDelta = {};
        break;
    }
    case EntityTemplate::UiLayoutGroup:
        element.AddComponent<UiLayoutGroup>();
        rect.layout.sizeDelta = {400, 300};
        break;
    default:
        break;
    }
    if (kind == EntityTemplate::UiButton || kind == EntityTemplate::UiToggle) {
        auto& label = element.CreateChild("Label");
        auto& labelRect = label.AddComponent<UiRect>();
        auto& text = label.AddComponent<UiText>();
        text.font = fontRef;
        text.text = kind == EntityTemplate::UiButton ? "Button" : "Toggle";
        if (kind == EntityTemplate::UiButton) {
            labelRect.layout.anchorMin = {};
            labelRect.layout.anchorMax = {1, 1};
            labelRect.layout.sizeDelta = {};
            text.settings.alignment = UiTextAlignment::Center;
            labelRect.layout.offset = {0, -6};
        } else {
            labelRect.layout.anchorMin = labelRect.layout.anchorMax = {1, 0.5f};
            labelRect.layout.pivot = {0, 0.5f};
            labelRect.layout.offset = {8, 0};
            labelRect.layout.sizeDelta = {150, 24};
        }
    }
    return scene.SaveToString();
}

class CreateUiWidgetCommand final : public SceneCommand {
public:
    CreateUiWidgetCommand(SceneDocument& document, EntityTemplate kind, EntityId parent, bool wrap, AssetId font)
        : SceneCommand(document), m_create(document, "Create UI", MakeUiTemplate(kind, wrap, font), parent),
          m_wrap(wrap) {}
    std::string GetName() const override { return "Create UI"; }
    bool Execute() override {
        if (!m_create.Execute())
            return false;
        const auto roots = m_create.GetCreated();
        if (roots.empty())
            return false;
        auto* entity = GetScene().GetEntity(roots.front());
        if (m_wrap) {
            const auto children = entity->GetChildren();
            if (!children.empty())
                entity = children.front();
        }
        m_selected = entity->GetId();
        return true;
    }
    void Undo() override { m_create.Undo(); }
    std::optional<std::vector<EntityId>> GetSelectionAfter() const override { return std::vector{m_selected}; }

private:
    CreateEntitiesCommand m_create;
    bool m_wrap;
    EntityId m_selected = EntityId::Invalid;
};
} // namespace

bool IsUiTemplate(EntityTemplate kind) {
    return kind >= EntityTemplate::Canvas && kind <= EntityTemplate::UiLayoutGroup;
}
bool CreateUiWidget(SceneDocument& document, EntityTemplate kind, EntityId parent) {
    if (kind != EntityTemplate::Canvas && parent == EntityId::Invalid) {
        if (const auto* canvas = document.GetScene().FindComponent<Canvas>())
            parent = canvas->GetEntity().GetId();
    }
    const auto* parentEntity = document.GetScene().GetEntity(parent);
    const bool wrap = kind != EntityTemplate::Canvas && !(parentEntity && parentEntity->GetComponentInParent<Canvas>());
    AssetId font;
    if (auto* assets = document.GetAssetManager())
        font = assets->FindByPath("fonts/FiraSans-Regular.ttf").value_or(AssetId{});
    return document.Execute(std::make_unique<CreateUiWidgetCommand>(document, kind, parent, wrap, font));
}

} // namespace Spot::Editor
