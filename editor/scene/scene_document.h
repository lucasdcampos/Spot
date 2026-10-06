#pragma once

#include "core/command_history.h"
#include "scene/selection.h"

#include <spot.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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
    // The entity with the id, which a consistent history always finds.
    Entity& GetEntity(EntityId id) const;

private:
    SceneDocument& m_document;
};

// A scene open in the editor: its entities, never run, with their selection and the history of changes to them.
class SceneDocument {
public:
    // A new scene that is not saved anywhere yet.
    explicit SceneDocument(std::string name = "Untitled");

    // Loads a scene file, whose asset id, when it has one, identifies the document. Returns null, after logging why,
    // when it cannot be read.
    static std::unique_ptr<SceneDocument> Open(const std::filesystem::path& file, AssetId asset = {},
                                               AssetManager* assets = nullptr);

    SceneDocument(const SceneDocument&) = delete;
    SceneDocument& operator=(const SceneDocument&) = delete;

    Scene& GetScene() { return *m_scene; }
    const Scene& GetScene() const { return *m_scene; }
    CommandHistory& GetHistory() { return m_history; }
    Selection& GetSelection() { return m_selection; }
    const Selection& GetSelection() const { return m_selection; }

    // The file's name without its extension, or the name given to a new scene.
    const std::string& GetName() const { return m_name; }
    // Empty until the scene is saved.
    const std::filesystem::path& GetFile() const { return m_file; }
    AssetId GetAssetId() const { return m_asset; }
    // Never repeats while the editor runs, so windows can tell documents apart, even with the same name.
    std::uint64_t GetId() const { return m_id; }

    bool IsDirty() const { return m_history.IsDirty(); }
    // Saves to the document's file, which it must have.
    bool Save();
    // Saves to another file, which becomes the document's.
    bool SaveAs(const std::filesystem::path& file, AssetId asset = {});

    // Executes the command in the history and selects what it says to.
    bool Execute(std::unique_ptr<SceneCommand> command);
    void Undo();
    void Redo();

private:
    std::unique_ptr<Scene> m_scene = std::make_unique<Scene>();
    CommandHistory m_history;
    Selection m_selection;
    std::string m_name;
    std::filesystem::path m_file;
    AssetId m_asset;
    std::uint64_t m_id;
};

} // namespace Spot::Editor
