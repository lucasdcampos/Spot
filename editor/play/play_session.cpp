#include "play/play_session.h"

#include "core/editor_log.h"
#include <imgui.h>

#include <utility>

namespace Spot::Editor {

PlaySession::PlaySession(Application& application, SceneDocument& source, AssetManager& assets,
                         std::optional<GameSettings> game)
    : m_application(application), m_source(source) {
    if (const GameSettings* previous = application.GetGameSettings()) {
        m_previousGame = *previous;
    }
    // Set before the copy loads, so its scene gets the game's settings.
    if (game) {
        application.SetGameSettings(std::move(game));
    }
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
    // The view gets focus once it is clicked, which hands the input over.
    m_editorCapture = m_input.PushCapture("Editor");
    GetEditorLogger().Info("Playing '{}'", source.GetName());
}

PlaySession::~PlaySession() {
    // What is selected in the copy stays selected in the document where it can.
    if (m_document) {
        SelectPaths(m_source.GetScene(), m_source.GetSelection(),
                    GetSelectedPaths(m_document->GetScene(), m_document->GetSelection()));
    }
    if (auto* scene = m_application.GetScene()) {
        if (auto* ui = scene->GetSystem<UiInputSystem>())
            ui->SetInputState(nullptr);
    }
    if (m_uiTextInput && !ImGui::GetIO().WantTextInput)
        m_application.GetWindow().StopTextInput();
    m_document.reset();
    m_application.LoadScene(nullptr);
    m_application.SetScenesPaused(false);
    if (m_cursorCaptured) {
        m_application.GetWindow().SetMouseCaptured(false);
    }
    Input::SetState(m_previousInput);
    m_application.SetGameSettings(std::move(m_previousGame));
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

void PlaySession::SetView(bool focused, Vector2 origin, Vector2 viewSize, Vector2 scale) {
    m_viewSize = viewSize;
    m_viewScale = scale;
    if (focused != m_focused) {
        if (focused) {
            m_input.PopCapture(std::exchange(m_editorCapture, InputCaptureId::None));
        } else {
            m_editorCapture = m_input.PushCapture("Editor");
        }
    }
    m_focused = focused;
    m_viewOrigin = origin;
    if (const bool capture = m_input.IsCursorLockApplied(); capture != m_cursorCaptured) {
        m_cursorCaptured = capture;
        m_application.GetWindow().SetMouseCaptured(capture);
    }
}

void PlaySession::Feed(const Event& event) {
    if (!m_focused)
        return;
    Event localEvent = event;
    if (const auto* moved = event.GetIf<MouseMovedEvent>()) {
        auto local = *moved;
        local.position = local.position - m_viewOrigin;
        localEvent = Event(local);
    }
    Event uiEvent = localEvent;
    if (const auto* moved = localEvent.GetIf<MouseMovedEvent>()) {
        auto pixel = *moved;
        pixel.position = {pixel.position.x * m_viewScale.x, pixel.position.y * m_viewScale.y};
        uiEvent = Event(pixel);
    }
    const bool consumed =
        m_application.GetScene() && m_application.GetScene()->HandleUiEvent(uiEvent, m_viewSize, &m_input);
    const bool text = UiInputSystem::IsTextInputCaptured(m_input);
    if (text && !m_application.GetWindow().IsTextInputActive())
        m_application.GetWindow().StartTextInput();
    else if (m_uiTextInput && !text && !ImGui::GetIO().WantTextInput)
        m_application.GetWindow().StopTextInput();
    m_uiTextInput = text;
    if (consumed) {
        m_input.ConsumeEvent(localEvent);
        return;
    }
    const auto* pressed = event.GetIf<KeyPressedEvent>();
    const auto* released = event.GetIf<KeyReleasedEvent>();
    if ((pressed && pressed->key == Key::Escape) || (released && released->key == Key::Escape))
        return;
    m_input.OnEvent(localEvent);
}

} // namespace Spot::Editor
