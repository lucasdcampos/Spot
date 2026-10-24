#pragma once

#include "play/play_host.h"
#include "scene/scene_document.h"

#include <spot.h>

#include <memory>
#include <optional>

namespace Spot::Editor {

// A scene document played as the game would run it: a copy of its scene loaded into the application, which updates it
// every frame, with its own input, while windows show and edit the copy. The copy, and whatever changed in it, goes
// away when the session ends; the document stays as it was.
class PlaySession {
public:
    // The scene is copied through the asset manager, which loads what it refers to. The game's settings, such as
    // its project's, are the application's while it plays.
    PlaySession(PlayHost& application, SceneDocument& source, AssetManager& assets,
                std::optional<GameSettings> game = std::nullopt);
    ~PlaySession();

    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;

    SceneDocument& GetSource() const { return m_source; }
    // The copy being played, for windows to show and edit; null once the game unloaded every scene.
    SceneDocument* GetDocument() const { return m_document.get(); }
    // Follows the game when it loads other scenes; called once a frame, before windows are drawn.
    void Update();

    bool IsPaused() const;
    void SetPaused(bool paused);
    // Runs one frame of the paused game.
    void Step();

    // Whether the game's view has focus, so the game gets the keyboard and the mouse.
    bool HasFocus() const { return m_focused; }
    // The view's rectangle in the window, which mouse positions are made relative to. While the view has no focus, the
    // editor holds a capture of the game's input (see InputState::PushCapture()), so the game gets nothing and its
    // cursor lock waits; with focus, the window's mouse is captured whenever the game locks the cursor.
    void SetView(bool focused, Vector2 origin, Vector2 viewSize = {}, Vector2 scale = {1, 1});
    // Gives the game an input event, with positions relative to its view. Escape stays the editor's, which takes the
    // keys and the mouse back with it, so games never see it.
    void Feed(const Event& event);

private:
    PlayHost& m_application;
    SceneDocument& m_source;
    Scene* m_scene = nullptr;
    bool m_loaded = false;
    std::unique_ptr<SceneDocument> m_document;
    InputState m_input;
    InputState* m_previousInput = nullptr;
    std::optional<GameSettings> m_previousGame;
    bool m_focused = false;
    bool m_paused = false;
    InputCaptureId m_editorCapture = InputCaptureId::None;
    bool m_cursorCaptured = false;
    Vector2 m_viewOrigin;
    Vector2 m_viewSize;
    Vector2 m_viewScale{1, 1};
    bool m_uiTextInput = false;
};

} // namespace Spot::Editor
