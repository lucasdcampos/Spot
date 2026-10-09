#include "play/play_session.h"

#include "core/editor_log.h"

#include <utility>

namespace Spot::Editor {

PlaySession::PlaySession(Application& application, SceneDocument& source, AssetManager& assets)
    : m_application(application), m_source(source) {
    auto scene = std::make_unique<Scene>();
    if (!scene->LoadFromString(source.GetScene().SaveToString(), &assets)) {
        GetEditorLogger().Error("Could not copy the scene '{}' to play it", source.GetName());
    }
    m_scene = scene.get();
    m_document = std::make_unique<SceneDocument>(source.GetName(), *m_scene);
    // The same entities stay selected, found by their places, since the copy's have other ids.
    SelectPaths(*m_scene, m_document->GetSelection(), GetSelectedPaths(source.GetScene(), source.GetSelection()));
    // Loaded on the next frame, when the game starts.
    m_application.LoadScene(std::move(scene));
    m_previousInput = Input::GetState();
    Input::SetState(&m_input);
    GetEditorLogger().Info("Playing '{}'", source.GetName());
}

PlaySession::~PlaySession() {
    // What is selected in the copy stays selected in the document where it can.
    if (m_document) {
        SelectPaths(m_source.GetScene(), m_source.GetSelection(),
                    GetSelectedPaths(m_document->GetScene(), m_document->GetSelection()));
    }
    m_document.reset();
    m_application.LoadScene(nullptr);
    m_application.GetPersistentScene().Clear();
    m_application.SetScenesPaused(false);
    if (m_cursorCaptured) {
        m_application.GetWindow().SetMouseCaptured(false);
    }
    Input::SetState(m_previousInput);
    GetEditorLogger().Info("Stopped playing '{}'", m_source.GetName());
}

void PlaySession::Update() {
    // The input of the last frame is done with: presses and movements only last a frame.
    m_input.BeginFrame();
    Scene* current = m_application.GetScene();
    if (!m_loaded) {
        m_loaded = current == m_scene;
        return;
    }
    if (current == m_scene) {
        return;
    }
    // The game loaded another scene, which windows show from now on.
    m_scene = current;
    m_document = current ? std::make_unique<SceneDocument>(m_source.GetName(), *current) : nullptr;
}

bool PlaySession::IsPaused() const {
    return m_application.AreScenesPaused();
}

void PlaySession::SetPaused(bool paused) {
    m_application.SetScenesPaused(paused);
}

void PlaySession::Step() {
    m_application.SetScenesPaused(true);
    m_application.StepScenes();
}

void PlaySession::SetView(bool focused, Vector2 origin) {
    if (m_focused && !focused) {
        // Keys and buttons held when the view loses focus would otherwise stay held, as when the window does.
        m_input.OnEvent(WindowFocusEvent{.focused = false});
    }
    m_focused = focused;
    m_viewOrigin = origin;
    if (const bool capture = focused && m_input.IsCursorLocked(); capture != m_cursorCaptured) {
        m_cursorCaptured = capture;
        m_application.GetWindow().SetMouseCaptured(capture);
    }
}

void PlaySession::Feed(const Event& event) {
    if (!m_focused) {
        return;
    }
    const auto* pressed = event.GetIf<KeyPressedEvent>();
    const auto* released = event.GetIf<KeyReleasedEvent>();
    if ((pressed && pressed->key == Key::Escape) || (released && released->key == Key::Escape)) {
        return;
    }
    if (const auto* moved = event.GetIf<MouseMovedEvent>()) {
        MouseMovedEvent local = *moved;
        local.position = local.position - m_viewOrigin;
        m_input.OnEvent(Event(local));
        return;
    }
    m_input.OnEvent(event);
}

} // namespace Spot::Editor
