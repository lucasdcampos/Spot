#include "scene/scene_workspace.h"

#include "app/editor_context.h"
#include "core/editor_log.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "project/project_templates.h"

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
const std::array SceneFilters{FileFilter{"Scenes", "scene"}};

json ToJson(Vector3 v) {
    return json::array({v.x, v.y, v.z});
}

Vector3 Vector3FromJson(const json& value) {
    if (!value.is_array() || value.size() != 3 || !std::ranges::all_of(value, &json::is_number)) {
        return {};
    }
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
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
    if (!asset || asset->type != AssetType::Scene) {
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
    if (document.GetFile().empty()) {
        SaveAs(document);
        return;
    }
    if (document.Save()) {
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
        &m_context.GetWindow(), SceneFilters,
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
            if (file.extension() != ".scene") {
                file += ".scene";
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
            project->Refresh();
            document.SetAssetId(
                project->GetAssetDatabase().FindByPath(PathToUtf8(relative.generic_u8string())).value_or(AssetId()));
            GetEditorLogger().Info("Saved scene '{}'", document.GetName());
            finish(true);
        },
        suggested);
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
                            {"speed", camera.GetSpeed()}}}});
    }
    json state = {{"scenes", scenes}};
    if (m_active && m_active->GetAssetId().IsValid()) {
        state["activeScene"] = m_active->GetAssetId().ToString();
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream(file, std::ios::binary) << state.dump(2) << '\n';
}

void SceneWorkspace::Draw(ImGuiID dockId) {
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

} // namespace Spot::Editor
