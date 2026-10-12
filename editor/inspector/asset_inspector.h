#pragma once

#include "app/editor_context.h"
#include "core/command_history.h"
#include "inspector/controller_graph.h"
#include "inspector/property_widgets.h"

#include <spot.h>
#include <spot_import.h>

#include <imgui.h>

#include <memory>
#include <optional>
#include <string>

namespace Spot::Editor {

// Shows an asset: what it is and where, a preview, and what can be changed about it: a material's properties, saved
// to its file at once with their own undo history, and the import settings of textures, models and fonts, which
// apply when asked to.
class AssetInspector {
public:
    explicit AssetInspector(EditorContext& context);

    void Draw(EditorProject& project, AssetId asset);
    // Puts a material edit in the history once its widget is let go (see EntityInspector::FinishEdit()).
    void FinishEdit();
    // The edits of the material shown, for undoing them; null when no material is shown.
    CommandHistory* GetHistory();

    // Where an item was drawn last frame, for tests to point at: "Apply", "Revert", "Extract", and fields of the
    // material or the import settings by pointer, as "/baseColor" or "/mipmaps".
    std::optional<std::pair<ImVec2, ImVec2>> GetItemRect(std::string_view item) const;

private:
    // A material's text before an edit in progress, and the widget being edited.
    struct Edit {
        std::string before;
        ImGuiID widget = 0;
    };

    // Starts over with another asset, or the same one after the project's assets changed.
    void Load(EditorProject& project, AssetId asset);
    void DrawHeader(EditorProject& project, const AssetInfo& info);
    void DrawPreview(EditorProject& project, const AssetInfo& info);
    void DrawController(EditorProject& project, const AssetInfo& info);
    void DrawClip(EditorProject& project, const AssetInfo& info);
    void DrawMaterial(EditorProject& project, const AssetInfo& info);
    void DrawImportSettings(EditorProject& project, const AssetInfo& info);
    void DrawSubAsset(EditorProject& project, const AssetInfo& info);
    void Record(const std::string& item);
    // Writes the material's text to its file and imports it, so scenes show it.
    bool SaveMaterial(EditorProject& project, const std::string& text);

    EditorContext& m_context;
    EditorProject* m_project = nullptr;
    AssetId m_asset;
    std::uint64_t m_version = 0;
    CommandHistory m_history;
    // The material shown, as edited, and as saved.
    std::optional<AnimatorController> m_controller;
    std::string m_controllerText;
    ControllerGraph m_graph;
    std::optional<MaterialSource> m_material;
    std::string m_materialText;
    std::optional<Edit> m_edit;
    double m_lastPreview = 0.0;
    // Import settings being changed, until applied or reverted.
    std::optional<AssetMeta> m_settings;
    EulerCache m_eulers;
    std::unordered_map<std::string, std::pair<ImVec2, ImVec2>> m_rects;
};

} // namespace Spot::Editor
