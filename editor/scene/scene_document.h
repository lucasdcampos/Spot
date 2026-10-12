#pragma once

#include "core/command_history.h"
#include "scene/animation_preview.h"
#include "scene/particle_preview.h"
#include "scene/selection.h"

#include <spot.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Spot::Editor {

class SceneDocument;

// A change to a scene document. Besides changing the scene, it may say what to select once done, such as the
// entities it created; undoing it brings back what was selected before.
class SceneCommand : public Command {
public:
    explicit SceneCommand(SceneDocument& document) : m_document(document) {}

    virtual std::optional<std::vector<EntityId>> GetSelectionAfter() const { return std::nullopt; }

protected:
    SceneDocument& GetDocument() const { return m_document; }
    Scene& GetScene() const;
    // For immediate command construction. Replaying history checks for targets removed by prefab updates.
    Entity& GetEntity(EntityId id) const;

private:
    SceneDocument& m_document;
};

// A scene open in the editor: its entities, never run, with their selection and the history of changes to them.
class SceneDocument {
public:
    // A new scene that is not saved anywhere yet.
    explicit SceneDocument(std::string name = "Untitled");
    // Edits a scene owned elsewhere, such as the copy of a scene a game plays, which must outlive the document.
    SceneDocument(std::string name, Scene& scene);

    // Loads a scene file, whose asset id, when it has one, identifies the document. Returns null, after logging why,
    // when it cannot be read.
    static std::unique_ptr<SceneDocument> Open(const std::filesystem::path& file, AssetId asset = {},
                                               AssetManager* assets = nullptr);

    SceneDocument(const SceneDocument&) = delete;
    SceneDocument& operator=(const SceneDocument&) = delete;

    Scene& GetScene() { return *m_scene; }
    const Scene& GetScene() const { return *m_scene; }
    AnimationPreview& GetAnimationPreview() { return m_animationPreview; }
    ParticlePreview& GetParticlePreview() { return m_particlePreview; }
    CommandHistory& GetHistory() { return m_history; }
    Selection& GetSelection() { return m_selection; }
    const Selection& GetSelection() const { return m_selection; }

    // The file's name without its extension, or the name given to a new scene.
    const std::string& GetName() const { return m_name; }
    // Empty until the scene is saved.
    const std::filesystem::path& GetFile() const { return m_file; }
    bool IsPrefab() const { return m_prefabMode; }
    AssetId GetAssetId() const { return m_asset; }
    AssetManager* GetAssetManager() const { return m_assets; }
    void SetAssetManager(AssetManager* assets) { m_assets = assets; }
    // Set once a scene saved to a new file is imported and so becomes an asset.
    void SetAssetId(AssetId asset) { m_asset = asset; }
    // Follows the scene's file when it moves, taking its new name; without a file, as after it was deleted, the
    // scene is no longer an asset and saving it asks where to.
    void SetFile(std::filesystem::path file, AssetId asset);
    // Never repeats while the editor runs, so windows can tell documents apart, even with the same name.
    std::uint64_t GetId() const { return m_id; }

    // A scene that was never saved counts as changed, so closing it asks whether to keep it.
    bool IsDirty() const { return m_file.empty() || m_history.IsDirty(); }
    // Saves to the document's file, which it must have.
    bool Save();
    // Saves to another file, which becomes the document's.
    bool SaveAs(const std::filesystem::path& file, AssetId asset = {});

    // Executes the command in the history and selects what it says to.
    bool Execute(std::unique_ptr<SceneCommand> command);
    void Undo();
    void Redo();

    // Reloads affected prefab instances from their current assets, preserving ids, overrides and history. A failed
    // dependency leaves the document intact. Running scenes must use their existing instances until play stops.
    bool RefreshPrefabs(std::span<const AssetId> changed);
    // Changes when entities are replaced outside the command history, for inspector caches and edits in progress.
    std::uint64_t GetSceneRevision() const { return m_sceneRevision; }

    // Stages a replacement before changing the document; history remains owned by its caller.
    bool ReplaceSceneText(std::string_view text);

private:
    std::unique_ptr<Scene> m_ownedScene;
    Scene* m_scene;
    CommandHistory m_history;
    Selection m_selection;
    AnimationPreview m_animationPreview;
    ParticlePreview m_particlePreview;
    std::string m_name;
    std::filesystem::path m_file;
    AssetId m_asset;
    AssetManager* m_assets = nullptr;
    bool m_prefabMode = false;
    std::uint64_t m_id;
    std::uint64_t m_sceneRevision = 0;
};

} // namespace Spot::Editor
