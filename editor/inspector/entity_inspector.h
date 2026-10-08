#pragma once

#include "app/editor_context.h"
#include "inspector/property_widgets.h"
#include "scene/scene_commands.h"

#include <spot.h>

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Spot::Editor {

// Shows and edits the selected entities of a scene: their name, activity and tags, their transform and their
// components. Edits reach every selected entity, and each becomes one undoable step, however long its drag lasts.
class EntityInspector {
public:
    explicit EntityInspector(EditorContext& context);

    // Draws into the current window. The document's primary selected entity must exist.
    void Draw(SceneDocument& document);
    // Puts the edit in progress in its document's history once its widget is let go. Called each frame before
    // anything can undo, so an edit is always in the history before it can be undone.
    void FinishEdit();

    // Where an item was drawn last frame, in screen coordinates, as {min, max}, for tests to point at. Items are
    // "Active", "Name", "AddTag", "Transform/position" and the transform's other rows, "AddComponent", and for
    // components, by type, with "#1" after the type for the second of a type and so on: "PointLight/enabled",
    // "PointLight/menu", fields by pointer, as "PointLight/data/intensity", and menu items, as
    // "PointLight/menu/Remove".
    std::optional<std::pair<ImVec2, ImVec2>> GetItemRect(std::string_view item) const;
    // The values of the card's components that differ between the selected entities of the document, as pointers
    // into their saved form, as the card last showed them; the card is named as for GetItemRect().
    std::vector<std::string> GetMixedValues(const SceneDocument& document, std::string_view card) const;

private:
    // Components by their entity and their place among its components.
    using ComponentPlace = std::pair<EntityId, std::size_t>;

    // A component of the primary entity, and the matching component of each other selected entity: the one of the
    // same type that comes as many times after the others of that type.
    struct Card {
        std::string typeName;
        // Names its items for GetItemRect().
        std::string key;
        // The primary entity's first.
        std::vector<ComponentPlace> components;
    };

    // What an edit in progress changes, as it was before it.
    struct Edit {
        std::uint64_t document = 0;
        // The widget being edited; the edit ends when another one, or none, is active.
        ImGuiID widget = 0;
        std::string name;
        std::vector<ComponentPlace> components;
        std::vector<std::string> before;
        // The values changed, as pointers into the components' saved form.
        std::vector<std::string> changed;
        std::vector<SetTransformsCommand::Change> transforms;
    };

    // Which values differ between a card's components, kept while nothing could have changed them, since finding out
    // means saving every selected entity's component.
    struct MixedValues {
        std::uint64_t selection = 0;
        std::uint64_t history = 0;
        std::size_t components = 0;
        double time = 0.0;
        std::vector<std::string> pointers;
    };

    struct CardHeader {
        bool open = false;
        bool toggled = false;
    };

    void DrawHeader(SceneDocument& document, std::span<Entity* const> entities);
    void DrawTags(SceneDocument& document, std::span<Entity* const> entities);
    void DrawTransform(SceneDocument& document, std::span<Entity* const> entities);
    void DrawTransformMenu(SceneDocument& document, std::span<Entity* const> entities);
    void DrawComponents(SceneDocument& document, std::span<Entity* const> entities);
    void DrawComponent(SceneDocument& document, const Card& card);
    void DrawComponentMenu(SceneDocument& document, const Card& card, const std::string& text);
    const std::vector<std::string>& GetMixedValues(SceneDocument& document, const Card& card,
                                                   std::span<Component* const> components, const std::string& text);
    void DrawAddComponent(SceneDocument& document, std::span<Entity* const> entities);
    // A card's collapsible header, with an enabled checkbox when enabled is set and a button that opens the
    // "##menu" popup, as right-clicking it does.
    CardHeader DrawCardHeader(const std::string& key, const char* icon, const ImVec4& iconColor,
                              const std::string& title, std::optional<bool> enabled, bool enabledMixed);

    // Remembers where the last item was drawn, for GetItemRect().
    void Record(std::string item);
    // A menu item, remembered as the key followed by "/menu/" and the label.
    bool MenuItem(const std::string& key, const char* icon, const char* label, bool enabled = true);

    void BeginTransformEdit(SceneDocument& document, std::span<Entity* const> entities, const char* name);
    void BeginComponentEdit(SceneDocument& document, const Card& card, std::vector<std::string> before);
    void CommitEdit();
    // Executes the commands as one step of the document's history.
    static void Execute(SceneDocument& document, std::string name, std::vector<std::unique_ptr<SceneCommand>> commands);

    EditorContext& m_context;
    std::optional<Edit> m_edit;
    // By document and card.
    std::unordered_map<std::string, MixedValues> m_mixed;
    EulerCache m_transformEulers;
    EulerCache m_fieldEulers;
    std::string m_name;
    std::string m_newTag;
    std::string m_componentSearch;
    std::size_t m_componentHighlight = 0;
    bool m_uniformScale = false;
    // Changes to the entities' components wait until the cards are drawn, so none goes away while it is drawn.
    std::function<void()> m_afterDraw;
    std::unordered_map<std::string, std::pair<ImVec2, ImVec2>> m_rects;
};

} // namespace Spot::Editor
