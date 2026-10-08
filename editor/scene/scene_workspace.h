#pragma once

#include "play/play_session.h"
#include "scene/scene_document.h"
#include "viewport/scene_view.h"
#include "viewport/viewport_renderer.h"

#include <spot.h>

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Spot::Editor {

class EditorContext;

// The scenes open in the editor, each in its own view: opening, creating, saving and closing them, asking about
// unsaved changes, and remembering per project which were open and where their cameras were.
class SceneWorkspace {
public:
    explicit SceneWorkspace(EditorContext& context);
    ~SceneWorkspace();

    SceneWorkspace(const SceneWorkspace&) = delete;
    SceneWorkspace& operator=(const SceneWorkspace&) = delete;

    std::size_t GetDocumentCount() const { return m_scenes.size(); }
    SceneDocument& GetDocument(std::size_t index) { return *m_scenes[index].document; }
    // The open document with the id (see SceneDocument::GetId()); null when it was closed.
    SceneDocument* FindDocument(std::uint64_t id);
    // The document of the view focused last; null when none is open. While that document plays, the copy being
    // played, which windows edit meanwhile.
    SceneDocument* GetActiveDocument();
    SceneView* GetActiveView();
    bool HasUnsavedChanges() const;

    // A new scene with a sun and a sky, not saved anywhere yet.
    SceneDocument& NewScene();
    // Opens a scene asset of the project, or brings its view forward when it is open already. Null, after logging
    // why, when it cannot be read.
    SceneDocument* OpenScene(AssetId scene);
    // Opens a scene picked with the OS's dialog.
    void BrowseForScene();
    // Saves to the document's file, or asks for one when it has none.
    void Save(SceneDocument& document);
    // Asks where to save, inside the project's assets. Calls done with whether it saved.
    void SaveAs(SceneDocument& document, std::function<void(bool saved)> done = {});
    void SaveAll();
    // Closes the document, asking first when it has unsaved changes.
    void Close(SceneDocument& document);
    // Closes every document without asking.
    void CloseAll();
    // Asks whether to save the documents with unsaved changes among these, and runs proceed unless the user cancels.
    void ConfirmUnsavedChanges(std::vector<SceneDocument*> documents, std::function<void()> proceed);
    // The same, for documents known by their ids.
    void ConfirmUnsavedChanges(std::vector<std::uint64_t> documents, std::function<void()> proceed);
    void ConfirmAllUnsavedChanges(std::function<void()> proceed);

    // Opens what was open when the project was last closed, with the same views and selections, or its startup
    // scene.
    void RestoreState();
    void SaveState() const;

    // Plays the active document in the application: a copy of it runs as the game would, in the document's view.
    // Does nothing without an active document.
    void Play(Application& application);
    void StopPlaying();
    // Null while nothing plays.
    PlaySession* GetPlaySession() { return m_play.get(); }

    // Saves every open scene and clears it, runs between, then loads the scenes back with the same entity ids, so
    // their components are made anew while selections and histories stay as they were; this is how the game's code
    // is replaced under them. Nothing may play.
    void ReloadScenes(const std::function<void()>& between);

    // Draws the views, docking new ones into the node, and the prompts.
    void Draw(ImGuiID dockId);
    // Makes documents follow their scene files after the assets changed: moved files give their documents new
    // files and names, and deleted ones leave them without a file.
    void SyncWithAssets();
    // Draws the scenes into the views that showed this frame.
    void Render(RenderContext& context);

private:
    struct Opened {
        std::unique_ptr<SceneDocument> document;
        std::unique_ptr<SceneView> view;
    };

    SceneDocument& Add(std::unique_ptr<SceneDocument> document);
    void Remove(SceneDocument& document);
    Opened* Find(const SceneDocument& document);
    std::filesystem::path GetStateFile() const;
    void DrawEmptyArea(ImGuiID dockId);
    void DrawSavePrompt();

    EditorContext& m_context;
    std::vector<Opened> m_scenes;
    SceneDocument* m_active = nullptr;
    std::uint64_t m_assetsVersion = 0;
    std::unique_ptr<ViewportRenderer> m_renderer;
    // OS dialogs answer later; they only reach the workspace while this is alive.
    std::shared_ptr<SceneWorkspace*> m_self;

    // Documents waiting for the user to say whether to save them, by id, and what to do after.
    std::vector<std::uint64_t> m_prompted;
    std::function<void()> m_afterPrompt;
    bool m_openPrompt = false;
    // After the documents, so it goes first, as it refers to one of them.
    std::unique_ptr<PlaySession> m_play;
};

} // namespace Spot::Editor
