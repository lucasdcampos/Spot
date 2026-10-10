#include "scene/scene_workspace.h"

#include <spot_tools.h>

#include "app/editor_context.h"
#include "core/editor_log.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"
#include "scene/scene_commands.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace Spot::Editor {

using nlohmann::json;

namespace {

constexpr const char* SavePromptId = "Save Changes?###SaveChanges";
constexpr const char* RecoveryPromptId = "Recover Unsaved Changes###RecoverChanges";
constexpr std::string_view BackupFormat = "spot-scene-backup";
const std::array SceneFilters{FileFilter{"Scenes", "scene"}};
const std::array PrefabFilters{FileFilter{"Prefabs", "prefab"}};

json ToJson(Vector3 v) {
    return json::array({v.x, v.y, v.z});
}

Vector3 Vector3FromJson(const json& value) {
    if (!value.is_array() || value.size() != 3 || !std::ranges::all_of(value, &json::is_number)) {
        return {};
    }
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

// Entities get new ids each time a scene loads, so the selection is saved as the places of the entities.
json SaveSelection(const SceneDocument& document) {
    json selection = json::array();
    for (const HierarchyPath& path : GetSelectedPaths(document.GetScene(), document.GetSelection())) {
        selection.push_back(path);
    }
    return selection;
}

void RestoreSelection(SceneDocument& document, const json& selection) {
    if (!selection.is_array()) {
        return;
    }
    std::vector<HierarchyPath> paths;
    for (const json& path : selection) {
        if (path.is_array() && std::ranges::all_of(path, &json::is_number_unsigned)) {
            paths.push_back(path.get<HierarchyPath>());
        }
    }
    SelectPaths(document.GetScene(), document.GetSelection(), paths);
}

std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            joined += i + 1 == names.size() ? " and " : ", ";
        }
        joined += "'" + names[i] + "'";
    }
    return joined;
}

} // namespace

SceneWorkspace::SceneWorkspace(EditorContext& context)
    : m_context(context), m_self(std::make_shared<SceneWorkspace*>(this)) {
}

SceneWorkspace::~SceneWorkspace() {
    *m_self = nullptr;
}

SceneView* SceneWorkspace::GetActiveView() {
    Opened* open = m_active ? Find(*m_active) : nullptr;
    return open ? open->view.get() : nullptr;
}

SceneDocument* SceneWorkspace::GetActiveDocument() {
    if (m_play && m_active == &m_play->GetSource() && m_play->GetDocument()) {
        return m_play->GetDocument();
    }
    return m_active;
}

void SceneWorkspace::Play(Application& application) {
    EditorProject* project = m_context.GetProject();
    if (m_play || !m_active || m_active->IsPrefab() || !project) {
        return;
    }
    m_play = std::make_unique<PlaySession>(application, *m_active, project->GetAssets(),
                                           project->GetProject().GetGameSettings());
}

void SceneWorkspace::StopPlaying() {
    if (!m_play) {
        return;
    }
    if (Opened* open = Find(m_play->GetSource())) {
        open->view->StopShowingPlay();
    }
    m_play.reset();
}

void SceneWorkspace::ReloadScenes(const std::function<void()>& between) {
    SPOT_ASSERT(!m_play, "Scenes cannot be reloaded while one plays");
    std::vector<std::string> saved;
    for (Opened& open : m_scenes) {
        Scene& scene = open.document->GetScene();
        saved.push_back(scene.SaveToString());
        scene.Clear();
    }
    between();
    EditorProject* project = m_context.GetProject();
    for (std::size_t i = 0; i < m_scenes.size(); ++i) {
        m_scenes[i].document->GetScene().Instantiate(
            saved[i],
            {.keepIds = true, .keepExternalReferences = true, .assets = project ? &project->GetAssets() : nullptr});
    }
}

SceneDocument* SceneWorkspace::FindDocument(std::uint64_t id) {
    const auto found = std::ranges::find_if(m_scenes, [&](const Opened& open) { return open.document->GetId() == id; });
    return found != m_scenes.end() ? found->document.get() : nullptr;
}

bool SceneWorkspace::HasUnsavedChanges() const {
    return std::ranges::any_of(m_scenes, [](const Opened& open) { return open.document->IsDirty(); });
}

SceneDocument& SceneWorkspace::NewScene() {
    auto document = std::make_unique<SceneDocument>("Untitled");
    PopulateNewScene(document->GetScene());
    return Add(std::move(document));
}

SceneDocument* SceneWorkspace::OpenScene(AssetId scene) {
    for (Opened& open : m_scenes) {
        if (open.document->GetAssetId() == scene) {
            open.view->RequestFocus();
            return open.document.get();
        }
    }
    EditorProject* project = m_context.GetProject();
    const AssetInfo* asset = project ? project->GetAssetDatabase().Find(scene) : nullptr;
    if (!asset || (asset->type != AssetType::Scene && asset->type != AssetType::Prefab)) {
        GetEditorLogger().Error("Could not open the scene {}: the project has no such scene", scene.ToString());
        return nullptr;
    }
    const std::filesystem::path file = project->GetProject().GetAssetsDirectory() / Utf8ToPath(asset->path);
    auto document = SceneDocument::Open(file, scene, &project->GetAssets());
    return document ? &Add(std::move(document)) : nullptr;
}

void SceneWorkspace::BrowseForScene() {
    EditorProject* project = m_context.GetProject();
    if (!project) {
        return;
    }
    FileDialog::OpenFile(
        &m_context.GetWindow(), SceneFilters,
        [self = std::weak_ptr(m_self)](std::vector<std::filesystem::path> paths) {
            const auto workspace = self.lock();
            if (!workspace || !*workspace || paths.empty()) {
                return;
            }
            SceneWorkspace& owner = **workspace;
            EditorProject* current = owner.m_context.GetProject();
            if (!current) {
                return;
            }
            const std::filesystem::path relative =
                paths.front().lexically_relative(current->GetProject().GetAssetsDirectory());
            const auto id = current->GetAssetDatabase().FindByPath(PathToUtf8(relative.generic_u8string()));
            if (!id) {
                GetEditorLogger().Error("'{}' is not a scene of the project's assets", PathToUtf8(paths.front()));
                return;
            }
            owner.OpenScene(*id);
        },
        false, project->GetProject().GetAssetsDirectory());
}

void SceneWorkspace::Save(SceneDocument& document) {
    if (m_play && &document == m_play->GetDocument()) {
        GetEditorLogger().Warning("Stop playing to save '{}'; what changes while playing is undone when it stops",
                                  document.GetName());
        return;
    }
    if (document.GetFile().empty()) {
        SaveAs(document);
        return;
    }
    if (document.Save()) {
        DropBackup(document);
        if (EditorProject* project = m_context.GetProject()) {
            project->Refresh();
        }
        GetEditorLogger().Info("Saved scene '{}'", document.GetName());
    }
}

void SceneWorkspace::SaveAs(SceneDocument& document, std::function<void(bool saved)> done) {
    EditorProject* project = m_context.GetProject();
    if (!project) {
        if (done) {
            done(false);
        }
        return;
    }
    const std::filesystem::path assets = project->GetProject().GetAssetsDirectory();
    const std::filesystem::path suggested =
        document.GetFile().empty() ? assets / "scenes" / Utf8ToPath(document.GetName() + ".scene") : document.GetFile();
    const std::uint64_t documentId = document.GetId();
    FileDialog::SaveFile(
        &m_context.GetWindow(), document.IsPrefab() ? PrefabFilters : SceneFilters,
        [self = std::weak_ptr(m_self), documentId, done = std::move(done)](std::vector<std::filesystem::path> paths) {
            const auto workspace = self.lock();
            SceneWorkspace* owner = workspace ? *workspace : nullptr;
            const auto finish = [&](bool saved) {
                if (done) {
                    done(saved);
                }
            };
            const auto found =
                owner ? std::ranges::find_if(owner->m_scenes,
                                             [&](const Opened& open) { return open.document->GetId() == documentId; })
                      : std::vector<Opened>::iterator();
            EditorProject* project = owner ? owner->m_context.GetProject() : nullptr;
            if (!owner || found == owner->m_scenes.end() || !project || paths.empty()) {
                finish(false);
                return;
            }
            std::filesystem::path file = paths.front();
            const auto extension = found->document->IsPrefab() ? ".prefab" : ".scene";
            if (file.extension() != extension) {
                file += extension;
            }
            const std::filesystem::path assetsDirectory = project->GetProject().GetAssetsDirectory();
            const std::filesystem::path relative = file.lexically_relative(assetsDirectory);
            if (relative.empty() || *relative.begin() == "..") {
                GetEditorLogger().Error("Scenes must be saved inside the project's assets folder, '{}'",
                                        PathToUtf8(assetsDirectory));
                finish(false);
                return;
            }
            std::error_code error;
            std::filesystem::create_directories(file.parent_path(), error);
            SceneDocument& document = *found->document;
            if (!document.SaveAs(file)) {
                finish(false);
                return;
            }
            owner->DropBackup(document);
            project->Refresh();
            document.SetAssetId(
                project->GetAssetDatabase().FindByPath(PathToUtf8(relative.generic_u8string())).value_or(AssetId()));
            GetEditorLogger().Info("Saved scene '{}'", document.GetName());
            finish(true);
        },
        suggested);
}

void SceneWorkspace::BackUp() {
    EditorProject* project = m_context.GetProject();
    if (!project || !m_recovery.empty()) {
        return;
    }
    for (Opened& open : m_scenes) {
        const SceneDocument& document = *open.document;
        if (!document.IsDirty()) {
            DropBackup(document);
            continue;
        }
        auto backup =
            std::ranges::find(m_backups, document.GetId(), &std::pair<std::uint64_t, std::filesystem::path>::first);
        if (backup == m_backups.end()) {
            m_backups.emplace_back(document.GetId(),
                                   GetBackupDirectory() / Utf8ToPath(AssetId::Generate().ToString() + ".json"));
            backup = m_backups.end() - 1;
        }
        const auto now = std::chrono::system_clock::now();
        const json content = {
            {"format", BackupFormat},
            {"asset", document.GetAssetId().IsValid() ? document.GetAssetId().ToString() : std::string()},
            {"name", document.GetName()},
            {"time", std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()},
            {"scene", document.GetScene().SaveToString()},
        };
        std::error_code error;
        std::filesystem::create_directories(backup->second.parent_path(), error);
        std::ofstream stream(backup->second, std::ios::binary);
        stream << content.dump(-1, ' ', false, json::error_handler_t::replace);
        if (!stream) {
            GetEditorLogger().Warning("Could not back up the scene '{}' to '{}'", document.GetName(),
                                      PathToUtf8(backup->second));
        }
    }
}

std::vector<SceneBackup> SceneWorkspace::FindBackups() const {
    std::vector<SceneBackup> backups;
    EditorProject* project = m_context.GetProject();
    if (!project) {
        return backups;
    }
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(GetBackupDirectory(), error)) {
        std::ifstream stream(entry.path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        const json content = json::parse(text, nullptr, false);
        if (!content.is_object() || content.value("format", std::string()) != BackupFormat ||
            !content.contains("scene") || !content["scene"].is_string()) {
            continue;
        }
        SceneBackup backup;
        backup.file = entry.path();
        backup.asset = AssetId::Parse(content.value("asset", std::string())).value_or(AssetId());
        backup.name = content.value("name", std::string("Untitled"));
        backup.time =
            std::chrono::system_clock::time_point(std::chrono::milliseconds(content.value("time", std::int64_t{0})));
        backup.scene = content["scene"].get<std::string>();

        // A scene saved after it was backed up has all the backup had.
        const AssetInfo* asset = backup.asset.IsValid() ? project->GetAssetDatabase().Find(backup.asset) : nullptr;
        if (asset) {
            const std::filesystem::path sceneFile =
                project->GetProject().GetAssetsDirectory() / Utf8ToPath(asset->path);
            const auto sceneTime = std::filesystem::last_write_time(sceneFile, error);
            if (!error && sceneTime > std::filesystem::last_write_time(entry.path(), error)) {
                continue;
            }
        } else {
            // The scene is gone, so its changes come back as a new scene.
            backup.asset = AssetId();
        }
        backups.push_back(std::move(backup));
    }
    std::ranges::sort(backups, {}, &SceneBackup::time);
    return backups;
}

void SceneWorkspace::OfferRecovery() {
    m_recovery = FindBackups();
    if (m_recovery.empty()) {
        // Only backups the scenes' files made useless, if any.
        DiscardBackups();
        return;
    }
    m_openRecovery = true;
}

void SceneWorkspace::RecoverBackups() {
    std::vector<SceneBackup> backups = std::exchange(m_recovery, {});
    if (backups.empty()) {
        backups = FindBackups();
    }
    for (const SceneBackup& backup : backups) {
        SceneDocument* document = backup.asset.IsValid() ? OpenScene(backup.asset) : nullptr;
        if (!document) {
            document = &Add(std::make_unique<SceneDocument>(backup.name));
        }
        document->Execute(std::make_unique<ReplaceSceneCommand>(*document, backup.scene, "Recover Unsaved Changes"));
        GetEditorLogger().Info("Recovered the unsaved changes to the scene '{}'", backup.name);
    }
    DiscardBackups();
}

void SceneWorkspace::DiscardBackups() {
    m_recovery.clear();
    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(GetBackupDirectory(), error)) {
        files.push_back(entry.path());
    }
    for (const std::filesystem::path& file : files) {
        std::filesystem::remove(file, error);
    }
    m_backups.clear();
}

std::filesystem::path SceneWorkspace::GetBackupDirectory() const {
    const EditorProject* project = m_context.GetProject();
    return project ? project->GetProject().GetCacheDirectory() / "backups" : std::filesystem::path();
}

void SceneWorkspace::DropBackup(const SceneDocument& document) {
    const auto backup =
        std::ranges::find(m_backups, document.GetId(), &std::pair<std::uint64_t, std::filesystem::path>::first);
    if (backup == m_backups.end()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(backup->second, error);
    m_backups.erase(backup);
}

void SceneWorkspace::SaveAll() {
    for (Opened& open : m_scenes) {
        if (open.document->IsDirty() || open.document->GetFile().empty()) {
            Save(*open.document);
        }
    }
}

void SceneWorkspace::Close(SceneDocument& document) {
    ConfirmUnsavedChanges({&document}, [self = std::weak_ptr(m_self), id = document.GetId()] {
        const auto workspace = self.lock();
        if (!workspace || !*workspace) {
            return;
        }
        for (Opened& open : (*workspace)->m_scenes) {
            if (open.document->GetId() == id) {
                (*workspace)->Remove(*open.document);
                return;
            }
        }
    });
}

void SceneWorkspace::CloseAll() {
    StopPlaying();
    while (!m_scenes.empty()) {
        Remove(*m_scenes.back().document);
    }
}

void SceneWorkspace::ConfirmUnsavedChanges(std::vector<SceneDocument*> documents, std::function<void()> proceed) {
    std::vector<std::uint64_t> ids;
    for (const SceneDocument* document : documents) {
        ids.push_back(document->GetId());
    }
    ConfirmUnsavedChanges(std::move(ids), std::move(proceed));
}

void SceneWorkspace::ConfirmUnsavedChanges(std::vector<std::uint64_t> documents, std::function<void()> proceed) {
    m_prompted.clear();
    for (const Opened& open : m_scenes) {
        if (open.document->IsDirty() && std::ranges::find(documents, open.document->GetId()) != documents.end()) {
            m_prompted.push_back(open.document->GetId());
        }
    }
    if (m_prompted.empty()) {
        proceed();
        return;
    }
    m_afterPrompt = std::move(proceed);
    m_openPrompt = true;
}

void SceneWorkspace::ConfirmAllUnsavedChanges(std::function<void()> proceed) {
    std::vector<SceneDocument*> documents;
    for (Opened& open : m_scenes) {
        documents.push_back(open.document.get());
    }
    ConfirmUnsavedChanges(std::move(documents), std::move(proceed));
}

std::filesystem::path SceneWorkspace::GetStateFile() const {
    const EditorProject* project = m_context.GetProject();
    return project ? project->GetProject().GetCacheDirectory() / "editor.json" : std::filesystem::path();
}

void SceneWorkspace::RestoreState() {
    EditorProject* project = m_context.GetProject();
    if (!project) {
        return;
    }
    std::ifstream stream(GetStateFile(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const json state = json::parse(text, nullptr, false);
    const auto scenes = state.is_object() ? state.find("scenes") : state.end();
    if (!state.is_object() || scenes == state.end() || !scenes->is_array() || scenes->empty()) {
        if (const AssetId startup = project->GetProject().GetSettings().startupScene; startup.IsValid()) {
            if (SceneDocument* document = OpenScene(startup)) {
                m_active = document;
            }
        }
        return;
    }

    const std::string active = state.value("activeScene", std::string());
    for (const json& saved : *scenes) {
        const auto id = saved.is_object() ? AssetId::Parse(saved.value("id", std::string())) : std::nullopt;
        if (!id || !project->GetAssetDatabase().Find(*id)) {
            continue;
        }
        SceneDocument* document = OpenScene(*id);
        if (!document) {
            continue;
        }
        if (const auto camera = saved.find("camera"); camera != saved.end() && camera->is_object()) {
            EditorCamera& view = Find(*document)->view->GetCamera();
            view.SetView(Vector3FromJson(camera->value("pivot", json())), camera->value("yaw", view.GetYaw()),
                         camera->value("pitch", view.GetPitch()), camera->value("distance", view.GetDistance()));
            view.SetSpeed(camera->value("speed", view.GetSpeed()));
            const Vector3 center2D = Vector3FromJson(camera->value("center2D", json()));
            view.SetCenter2D({center2D.x, center2D.y});
            view.SetSize2D(camera->value("size2D", view.GetSize2D()));
            view.Set2D(camera->value("mode2D", false));
        }
        if (const auto selection = saved.find("selection"); selection != saved.end()) {
            RestoreSelection(*document, *selection);
        }
        if (saved.value("id", std::string()) == active) {
            m_active = document;
            Find(*document)->view->RequestFocus();
        }
    }
}

void SceneWorkspace::SaveState() const {
    const std::filesystem::path file = GetStateFile();
    if (file.empty()) {
        return;
    }
    json scenes = json::array();
    for (const Opened& open : m_scenes) {
        if (!open.document->GetAssetId().IsValid()) {
            continue;
        }
        const EditorCamera& camera = open.view->GetCamera();
        scenes.push_back({{"id", open.document->GetAssetId().ToString()},
                          {"camera",
                           {{"pivot", ToJson(camera.GetPivot())},
                            {"yaw", camera.GetYaw()},
                            {"pitch", camera.GetPitch()},
                            {"distance", camera.GetDistance()},
                            {"speed", camera.GetSpeed()},
                            {"mode2D", camera.Is2D()},
                            {"center2D", ToJson({camera.GetCenter2D().x, camera.GetCenter2D().y, 0.0f})},
                            {"size2D", camera.GetSize2D()}}},
                          {"selection", SaveSelection(*open.document)}});
    }
    json state = {{"scenes", scenes}};
    if (m_active && m_active->GetAssetId().IsValid()) {
        state["activeScene"] = m_active->GetAssetId().ToString();
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream(file, std::ios::binary) << state.dump(2) << '\n';
}

void SceneWorkspace::SyncWithAssets() {
    EditorProject* project = m_context.GetProject();
    if (!project) {
        return;
    }
    m_assetsVersion = project->GetVersion();
    // Scenes show the new versions of assets that changed, such as a texture edited in another application.
    const std::vector<AssetId> reloaded = project->TakeReloadedAssets();
    if (!reloaded.empty()) {
        for (const Opened& open : m_scenes) {
            open.document->GetScene().ForEachEntity([&](Entity& entity) {
                for (Component* component : entity.GetComponents<Component>()) {
                    ReloadAssetReferences(*component, reloaded, &project->GetAssets());
                }
            });
        }
    }
    for (const Opened& open : m_scenes) {
        SceneDocument& document = *open.document;
        if (!document.GetAssetId().IsValid()) {
            continue;
        }
        const AssetInfo* info = project->GetAssetDatabase().Find(document.GetAssetId());
        if (!info) {
            GetEditorLogger().Warning("The file of the open scene '{}' is gone; saving it asks where to",
                                      document.GetName());
            document.SetFile({}, {});
            continue;
        }
        const std::filesystem::path file = project->GetProject().GetAssetsDirectory() / Utf8ToPath(info->path);
        if (file != document.GetFile()) {
            document.SetFile(file, info->id);
        }
    }
}

void SceneWorkspace::Draw(ImGuiID dockId) {
    if (m_play) {
        m_play->Update();
    }
    if (const EditorProject* project = m_context.GetProject(); project && project->GetVersion() != m_assetsVersion) {
        SyncWithAssets();
    }
    if (m_scenes.empty()) {
        DrawEmptyArea(dockId);
    }
    // By index, since closing a view removes it.
    for (std::size_t i = 0; i < m_scenes.size(); ++i) {
        Opened& open = m_scenes[i];
        const SceneView::DrawResult result = open.view->Draw(m_context, m_renderer.get(), dockId);
        if (result.focused) {
            m_active = open.document.get();
        }
        if (result.closeRequested) {
            Close(*open.document);
        }
    }
    DrawSavePrompt();
    DrawRecoveryPrompt();
}

void SceneWorkspace::Render(RenderContext& context) {
    if (!m_renderer) {
        if (GraphicsDevice* device = m_context.GetGraphicsDevice()) {
            m_renderer = std::make_unique<ViewportRenderer>(*device);
        }
    }
    if (!m_renderer) {
        return;
    }
    for (Opened& open : m_scenes) {
        open.view->Render(context, *m_renderer);
    }
}

SceneDocument& SceneWorkspace::Add(std::unique_ptr<SceneDocument> document) {
    if (EditorProject* project = m_context.GetProject()) {
        document->SetAssetManager(&project->GetAssets());
    }
    SceneDocument& added = *document;
    auto view = std::make_unique<SceneView>(added);
    view->RequestFocus();
    m_scenes.push_back({std::move(document), std::move(view)});
    m_active = &added;
    return added;
}

void SceneWorkspace::Remove(SceneDocument& document) {
    const auto found =
        std::ranges::find_if(m_scenes, [&](const Opened& open) { return open.document.get() == &document; });
    if (found == m_scenes.end()) {
        return;
    }
    if (m_play && &m_play->GetSource() == &document) {
        StopPlaying();
    }
    DropBackup(document);
    m_scenes.erase(found);
    if (m_active == &document) {
        m_active = m_scenes.empty() ? nullptr : m_scenes.back().document.get();
    }
}

SceneWorkspace::Opened* SceneWorkspace::Find(const SceneDocument& document) {
    const auto found =
        std::ranges::find_if(m_scenes, [&](const Opened& open) { return open.document.get() == &document; });
    return found != m_scenes.end() ? &*found : nullptr;
}

void SceneWorkspace::DrawEmptyArea(ImGuiID dockId) {
    ImGui::SetNextWindowDockID(dockId, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(ICON_FA_CUBE "  Scene###Scene", nullptr, ImGuiWindowFlags_NoCollapse)) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const char* hint = "No scene is open";
        const char* detail = "Create a scene, or open one of the project's.";
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonWidth = ImGui::GetFontSize() * 9.0f;
        const float height = ImGui::GetTextLineHeightWithSpacing() * 2.5f + ImGui::GetFrameHeightWithSpacing();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max((available.y - height) * 0.5f, 0.0f));
        ImGui::PushFont(GetBoldFont(), style.FontSizeBase * 1.3f);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(hint).x) * 0.5f);
        ImGui::TextUnformatted(hint);
        ImGui::PopFont();
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(detail).x) * 0.5f);
        ImGui::TextColored(EditorColors::TextMuted, "%s", detail);
        ImGui::Spacing();
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth * 2.0f - style.ItemSpacing.x) * 0.5f);
        if (ImGui::Button(ICON_FA_FILE "  New Scene", {buttonWidth, 0.0f})) {
            NewScene();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open Scene...", {buttonWidth, 0.0f})) {
            BrowseForScene();
        }
    }
    ImGui::End();
}

void SceneWorkspace::DrawSavePrompt() {
    if (m_openPrompt) {
        ImGui::OpenPopup(SavePromptId);
        m_openPrompt = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::BeginPopupModal(SavePromptId, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    std::vector<SceneDocument*> documents;
    std::vector<std::string> names;
    for (Opened& open : m_scenes) {
        if (std::ranges::find(m_prompted, open.document->GetId()) != m_prompted.end()) {
            documents.push_back(open.document.get());
            names.push_back(open.document->GetName());
        }
    }

    ImGui::TextColored(EditorColors::Warning, ICON_FA_TRIANGLE_EXCLAMATION);
    ImGui::SameLine();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(std::format("Do you want to save the changes to {}?", JoinNames(names)).c_str());
    ImGui::TextColored(EditorColors::TextMuted, "Changes that are not saved will be lost.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const float buttonWidth = ImGui::GetFontSize() * 6.5f;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 3.0f - style.ItemSpacing.x * 2.0f -
                         style.WindowPadding.x);
    ImGui::PushStyleColor(ImGuiCol_Button, EditorColors::Accent);
    const bool save = ImGui::Button("Save", {buttonWidth, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Enter);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    const bool discard = ImGui::Button("Don't Save", {buttonWidth, 0.0f});
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", {buttonWidth, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape);

    if (save || discard || cancel) {
        ImGui::CloseCurrentPopup();
        std::function<void()> proceed = std::exchange(m_afterPrompt, {});
        const std::vector<std::uint64_t> prompted = std::exchange(m_prompted, {});
        if (discard) {
            proceed();
        } else if (save) {
            // Scenes with a file save at once; a new one asks where first, then the question comes back for the rest.
            const auto untitled = std::ranges::find_if(
                documents, [](const SceneDocument* document) { return document->GetFile().empty(); });
            if (untitled == documents.end()) {
                for (SceneDocument* document : documents) {
                    Save(*document);
                }
                proceed();
            } else {
                for (SceneDocument* document : documents) {
                    if (!document->GetFile().empty()) {
                        Save(*document);
                    }
                }
                SaveAs(**untitled, [self = std::weak_ptr(m_self), prompted, proceed](bool saved) {
                    const auto workspace = self.lock();
                    if (saved && workspace && *workspace) {
                        (*workspace)->ConfirmUnsavedChanges(prompted, proceed);
                    }
                });
            }
        }
    }
    ImGui::EndPopup();
}

void SceneWorkspace::DrawRecoveryPrompt() {
    if (m_openRecovery) {
        ImGui::OpenPopup(RecoveryPromptId);
        m_openRecovery = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::BeginPopupModal(RecoveryPromptId, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    ImGui::TextColored(EditorColors::Warning, ICON_FA_TRIANGLE_EXCLAMATION);
    ImGui::SameLine();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted("The editor stopped before these scenes were saved. Do you want to recover their changes?");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    for (const SceneBackup& backup : m_recovery) {
        ImGui::Bullet();
        ImGui::TextUnformatted(backup.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(EditorColors::TextMuted, "changed %s", Widgets::FormatRelativeTime(backup.time).c_str());
    }
    ImGui::Spacing();
    ImGui::TextColored(EditorColors::TextMuted, "Recovered changes can be undone, and are saved like any other.");
    ImGui::Spacing();

    const float buttonWidth = ImGui::GetFontSize() * 6.5f;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2.0f - style.ItemSpacing.x - style.WindowPadding.x);
    ImGui::PushStyleColor(ImGuiCol_Button, EditorColors::Accent);
    const bool recover = ImGui::Button("Recover", {buttonWidth, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Enter);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    const bool discard = ImGui::Button("Discard", {buttonWidth, 0.0f});
    if (recover || discard) {
        ImGui::CloseCurrentPopup();
        if (recover) {
            RecoverBackups();
        } else {
            DiscardBackups();
        }
    }
    ImGui::EndPopup();
}

} // namespace Spot::Editor
