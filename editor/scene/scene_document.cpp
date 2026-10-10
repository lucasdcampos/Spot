#include "scene/scene_document.h"

#include "core/editor_log.h"

#include <atomic>
#include <fstream>
#include <iterator>
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

SceneDocument::SceneDocument(std::string name)
    : m_ownedScene(std::make_unique<Scene>()), m_scene(m_ownedScene.get()), m_name(std::move(name)),
      m_id(s_nextDocumentId++) {
}

SceneDocument::SceneDocument(std::string name, Scene& scene)
    : m_scene(&scene), m_name(std::move(name)), m_id(s_nextDocumentId++) {
}

std::unique_ptr<SceneDocument> SceneDocument::Open(const std::filesystem::path& file, AssetId asset,
                                                   AssetManager* assets) {
    auto document = std::make_unique<SceneDocument>(PathToUtf8(file.stem()));
    document->m_prefabMode = file.extension() == ".prefab";
    bool loaded = false;
    if (document->m_prefabMode) {
        std::ifstream input(file, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (const auto prefab = Prefab::FromString(text)) {
            loaded =
                !document->m_scene->Instantiate(prefab->GetEntityText(), {.keepIds = true, .assets = assets}).empty() &&
                document->m_scene->GetRootEntities().size() == 1;
        }
    } else {
        loaded = document->m_scene->LoadFromFile(file, assets);
    }
    if (!loaded) {
        GetEditorLogger().Error("Could not open the scene '{}'", PathToUtf8(file));
        return nullptr;
    }
    document->m_file = file;
    document->m_asset = asset;
    document->m_assets = assets;
    return document;
}

void SceneDocument::SetFile(std::filesystem::path file, AssetId asset) {
    if (!file.empty()) {
        m_name = PathToUtf8(file.stem());
        m_prefabMode = file.extension() == ".prefab";
    }
    m_file = std::move(file);
    m_asset = asset;
}

bool SceneDocument::Save() {
    SPOT_ASSERT(!m_file.empty(), "The scene has no file yet; save it with SaveAs()");
    bool saved = false;
    if (m_prefabMode) {
        const auto prefab = Prefab::FromString(m_scene->SaveToString());
        if (!prefab) {
            GetEditorLogger().Error("A prefab must contain exactly one entity tree");
            return false;
        }
        std::ofstream output(m_file, std::ios::binary | std::ios::trunc);
        output << prefab->GetEntityText();
        output.close();
        saved = static_cast<bool>(output);
    } else {
        saved = m_scene->SaveToFile(m_file);
    }
    if (!saved) {
        GetEditorLogger().Error("Could not save the scene '{}'", PathToUtf8(m_file));
        return false;
    }
    m_history.MarkSaved();
    return true;
}

bool SceneDocument::SaveAs(const std::filesystem::path& file, AssetId asset) {
    const std::filesystem::path previous = std::exchange(m_file, file);
    const bool previousMode = std::exchange(m_prefabMode, file.extension() == ".prefab");
    if (!Save()) {
        m_file = previous;
        m_prefabMode = previousMode;
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
