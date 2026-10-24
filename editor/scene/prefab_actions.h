#pragma once

#include "scene/scene_document.h"

#include <spot_tools.h>

namespace Spot::Editor {

class EditorProject;

// A new source gets its identity from the asset database; the entity remains unchanged.
std::optional<AssetId> CreatePrefab(EditorProject& project, const Entity& entity, std::string_view folder);

enum class PrefabAction { Apply, Revert, Unpack };

class PrefabCommand final : public SceneCommand {
public:
    PrefabCommand(SceneDocument& document, EditorProject& project, EntityId root, PrefabAction action,
                  PrefabOverrideScope scope = {});

    std::string GetName() const override;
    bool Execute() override;
    void Undo() override;

private:
    bool WriteSource(std::string_view text);
    bool Restore(std::string_view scene, std::string_view source);

    EditorProject& m_project;
    PrefabAction m_action;
    AssetId m_asset;
    std::string m_before;
    std::string m_after;
    std::string m_sourceBefore;
    std::string m_sourceAfter;
};

} // namespace Spot::Editor
