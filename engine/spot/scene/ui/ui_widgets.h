#pragma once

#include "spot/assets/asset_ref.h"
#include "spot/core/color.h"
#include "spot/scene/component.h"
#include "spot/scene/signal.h"
#include "spot/scene/ui/ui_system.h"
#include "spot/scene/ui/ui_text_layout.h"

#include <string>

namespace Spot {

class Font;
class Texture;

enum class UiControlState { Normal, Hovered, Pressed, Focused, Disabled };
enum class UiEventType {
    Activate,
    PointerDown,
    PointerMove,
    PointerUp,
    Adjust,
    Text,
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
    Submit,
    Scroll,
    Cancel
};
struct UiEvent {
    UiEventType type = UiEventType::Activate;
    Vector2 position = {};
    float value = 0;
    std::string_view text = {};
};

struct UiControlStyle {
    Color normal = {0.12f, 0.14f, 0.18f, 1};
    Color hovered = {0.2f, 0.25f, 0.32f, 1};
    Color pressed = {0.08f, 0.1f, 0.14f, 1};
    Color focused = {0.2f, 0.35f, 0.5f, 1};
    Color disabled = {0.08f, 0.08f, 0.08f, 0.5f};
    Color accent = {0.2f, 0.65f, 0.9f, 1};
    // Drawn, tinted with the state's color, instead of a flat rectangle: a rounded panel, for instance, nine-sliced
    // by its borders (left, bottom, right, top, in its pixels) as UiImage does. The accent parts use it too.
    AssetRef<Texture> texture;
    Vector4 borders = {};
    Color GetColor(UiControlState state) const;
    void Serialize(Archive& archive);
};

class UiWidget : public Component {
public:
    bool IsInteractable() const { return m_interactable; }
    void SetInteractable(bool interactable) { m_interactable = interactable; }
    virtual bool IsFocusable() const { return false; }
    virtual bool WantsTextInput() const { return false; }
    UiControlState GetUiState() const { return m_interactable ? m_state : UiControlState::Disabled; }
    void SetUiState(UiControlState state) { m_state = state; }
    // All UI callbacks, including signals, run under the component failure guard.
    void HandleUiEvent(const UiEvent& event);
    const UiLayoutEntry* GetLayout() const;

protected:
    void OnDrawUi(Renderer2D& renderer, Vector2 viewSize) final;
    virtual void DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) = 0;
    virtual void OnUiEvent(const UiEvent& event);
    void SerializeWidget(Archive& archive);
    template <typename F>
    void Notify(F&& callback) {
        const Detail::DeferRemovalsScope defer(GetScene());
        if (!IsFaulted())
            InvokeHook("UiCallback", std::forward<F>(callback));
    }

private:
    bool m_interactable = true;
    UiControlState m_state = UiControlState::Normal;
};

class UiImage final : public UiWidget {
public:
    AssetRef<Texture> texture;
    Color tint = Color::White;
    Vector4 borders = {};

protected:
    void DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) override;
    void Serialize(Archive& archive) override;

private:
    UiBounds m_cachedBounds;
    Vector2 m_cachedTextureSize;
    Vector4 m_cachedBorders;
    float m_cachedScale = 0;
    std::vector<UiImageQuad> m_quads;
};

class UiText final : public UiWidget {
public:
    AssetRef<Font> font;
    std::string text = "Text";
    float fontSize = 20;
    Color color = Color::White;
    UiTextLayoutSettings settings;
    Vector2 GetPreferredSize() const;

protected:
    void DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) override;
    void Serialize(Archive& archive) override;

private:
    mutable AssetRef<Font> m_preferredFont;
    mutable std::string m_preferredText;
    mutable float m_preferredFontSize = 0;
    mutable Vector2 m_preferredSize;
    AssetRef<Font> m_cachedFont;
    std::string m_cachedText;
    Vector2 m_cachedBox;
    UiTextLayoutSettings m_cachedSettings;
    UiTextGeometry m_geometry;
};

class UiButton final : public UiWidget {
public:
    UiControlStyle style;
    Signal<> OnClick;
    bool IsFocusable() const override { return true; }
    void Click() { HandleUiEvent({.type = UiEventType::Activate}); }

protected:
    void DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) override;
    void OnUiEvent(const UiEvent& event) override;
    void Serialize(Archive& archive) override;
};

class UiToggle final : public UiWidget {
public:
    UiControlStyle style;
    Signal<bool> OnValueChanged;
    bool IsFocusable() const override { return true; }
    bool IsOn() const { return m_value; }
    void SetValue(bool value, bool notify = false);

protected:
    void DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) override;
    void OnUiEvent(const UiEvent& event) override;
    void Serialize(Archive& archive) override;

private:
    bool m_value = false;
};

class UiSlider final : public UiWidget {
public:
    UiControlStyle style;
    float minimum = 0;
    float maximum = 1;
    float step = 0.05f;
    bool vertical = false;
    Signal<float> OnValueChanged;
    bool IsFocusable() const override { return true; }
    float GetValue() const { return m_value; }
    void SetValue(float value, bool notify = false);

protected:
    void DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) override;
    void OnUiEvent(const UiEvent& event) override;
    void Serialize(Archive& archive) override;

private:
    float GetFraction() const;
    float m_value = 0;
};

class UiInputField final : public UiWidget {
public:
    UiControlStyle style;
    AssetRef<Font> font;
    float fontSize = 20;
    Color textColor = Color::White;
    std::string placeholder = "Enter text";
    int maxLength = 256;
    Signal<std::string_view> OnValueChanged;
    Signal<std::string_view> OnSubmit;
    bool IsFocusable() const override { return true; }
    bool WantsTextInput() const override { return true; }
    const std::string& GetText() const { return m_text; }
    void SetText(std::string text, bool notify = false);
    // Caret is a byte boundary in UTF-8. Left/right/delete operate on whole code points.
    std::size_t GetCaret() const { return m_caret; }

protected:
    void DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) override;
    void OnUiEvent(const UiEvent& event) override;
    void Serialize(Archive& archive) override;

private:
    std::string m_text;
    std::size_t m_caret = 0;
    std::string m_cachedText;
    AssetRef<Font> m_cachedFont;
    Vector2 m_cachedBox;
    UiTextGeometry m_geometry;
};

// Children use a content rectangle clipped to this viewport. Offset grows right and down.
class UiScrollView final : public UiWidget {
public:
    Vector2 contentSize = {0, 600};
    bool horizontal = false;
    bool vertical = true;
    float scrollSpeed = 40;
    UiControlStyle style;
    Signal<Vector2> OnValueChanged;
    Vector2 GetOffset() const { return m_offset; }
    void SetOffset(Vector2 offset, bool notify = false);
    UiBounds GetContentBounds(UiBounds viewport, float scale) const;
    bool IsFocusable() const override { return true; }

protected:
    void DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) override;
    void OnUiEvent(const UiEvent& event) override;
    void Serialize(Archive& archive) override;

private:
    Vector2 m_offset;
};

} // namespace Spot
