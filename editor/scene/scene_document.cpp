#include "scene/scene_document.h"

#include "core/editor_log.h"

#include <atomic>
#include <utility>

namespace Spot::Editor {

namespace {

std::atomic<std::uint64_t> s_nextDocumentId = 1;

// Keeps the selection before and after a scene command, so undo and redo restore it with the change.
class SelectingCommand final : public Command {
public:
    SelectingCommand(Selection& selection, std::unique_ptr<SceneCommand> command)
        : m_selection(selection), m_command(std::move(command)),
          m_before(selection.GetEntities().begin(), selection.GetEntities().end()) {}

    std::string GetName() const override { return m_command->GetName(); }

    bool Execute() override {
        if (!m_command->Execute()) {
            return false;
        }
        // Redoing brings back the selection the first execution ended with.
        if (m_executed) {
            m_selection.Set(m_after);
            return true;
        }
        if (const auto after = m_command->GetSelectionAfter()) {
            m_selection.Set(*after);
        }
        m_after.assign(m_selection.GetEntities().begin(), m_selection.GetEntities().end());
        m_executed = true;
        return true;
    }

    void Undo() override {
        m_command->Undo();
        m_selection.Set(m_before);
    }

    bool Merge(const Command& next) override {
        const auto* other = dynamic_cast<const SelectingCommand*>(&next);
        if (!other || !m_command->Merge(*other->m_command)) {
            return false;
        }
        m_after = other->m_after;
        return true;
    }

private:
    Selection& m_selection;
    std::unique_ptr<SceneCommand> m_command;
    std::vector<EntityId> m_before;
    std::vector<EntityId> m_after;
    bool m_executed = false;
};

} // namespace

Scene& SceneCommand::GetScene() const {
    return m_document.GetScene();
}

Entity& SceneCommand::GetEntity(EntityId id) const {
    Entity* entity = GetScene().GetEntity(id);
    SPOT_ASSERT(entity, "The history refers to an entity the scene does not have");
    return *entity;
}

SceneDocument::SceneDocument(std::string name) : m_name(std::move(name)), m_id(s_nextDocumentId++) {
}

std::unique_ptr<SceneDocument> SceneDocument::Open(const std::filesystem::path& file, AssetId asset,
                                                   AssetManager* assets) {
    auto document = std::make_unique<SceneDocument>(PathToUtf8(file.stem()));
    if (!document->m_scene->LoadFromFile(file, assets)) {
        GetEditorLogger().Error("Could not open the scene '{}'", PathToUtf8(file));
        return nullptr;
    }
    document->m_file = file;
    document->m_asset = asset;
    return document;
}

void SceneDocument::SetFile(std::filesystem::path file, AssetId asset) {
    if (!file.empty()) {
        m_name = PathToUtf8(file.stem());
    }
    m_file = std::move(file);
    m_asset = asset;
}

bool SceneDocument::Save() {
    SPOT_ASSERT(!m_file.empty(), "The scene has no file yet; save it with SaveAs()");
    if (!m_scene->SaveToFile(m_file)) {
        GetEditorLogger().Error("Could not save the scene '{}'", PathToUtf8(m_file));
        return false;
    }
    m_history.MarkSaved();
    return true;
}

bool SceneDocument::SaveAs(const std::filesystem::path& file, AssetId asset) {
    const std::filesystem::path previous = std::exchange(m_file, file);
    if (!Save()) {
        m_file = previous;
        return false;
    }
    m_asset = asset;
    m_name = PathToUtf8(file.stem());
    return true;
}

bool SceneDocument::Execute(std::unique_ptr<SceneCommand> command) {
    return m_history.Execute(std::make_unique<SelectingCommand>(m_selection, std::move(command)));
}

void SceneDocument::Undo() {
    m_history.Undo();
    m_selection.RemoveMissing(*m_scene);
}

void SceneDocument::Redo() {
    m_history.Redo();
    m_selection.RemoveMissing(*m_scene);
}

} // namespace Spot::Editor
