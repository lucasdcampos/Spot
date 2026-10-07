#pragma once

#include "scene/scene_document.h"

#include <spot.h>

#include <memory>

namespace Spot::Editor {

// A scene document played as the game would run it: a copy of its scene loaded into the application, which updates it
// every frame, with its own input, while windows show and edit the copy. The copy, and whatever changed in it, goes
// away when the session ends; the document stays as it was.
class PlaySession {
public:
    // The scene is copied through the asset manager, which loads what it refers to.
    PlaySession(Application& application, SceneDocument& source, AssetManager& assets);
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
    // The view's rectangle in the window, which mouse positions are made relative to.
    void SetView(bool focused, Vector2 origin);
    // Gives the game an input event, with positions relative to its view, while it has focus.
    void Feed(const Event& event);

private:
    Application& m_application;
    SceneDocument& m_source;
    Scene* m_scene = nullptr;
    bool m_loaded = false;
    std::unique_ptr<SceneDocument> m_document;
    InputState m_input;
    const InputState* m_previousInput = nullptr;
    bool m_focused = false;
    Vector2 m_viewOrigin;
};

} // namespace Spot::Editor
