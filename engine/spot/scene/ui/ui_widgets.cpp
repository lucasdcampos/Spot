#include "spot/scene/ui/ui_widgets.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/io/archive.h"
#include "spot/core/io/utf8.h"
#include "spot/graphics/render_context.h"
#include "spot/graphics/texture.h"
#include "spot/render/font.h"
#include "spot/render/renderer_2d.h"
#include "spot/scene/entity.h"
#include "spot/scene/scene.h"

#include <algorithm>
#include <cmath>

namespace Spot {
namespace {
float Positive(float value, float fallback = 0) {
    return std::isfinite(value) ? std::max(0.0f, value) : fallback;
}
void Clip(Renderer2D& renderer, UiBounds bounds, Vector2 viewSize) {
    bounds = IntersectUiBounds(bounds, {{}, viewSize});
    const auto x = static_cast<std::int32_t>(std::floor(bounds.min.x));
    const auto y = static_cast<std::int32_t>(std::floor(viewSize.y - bounds.max.y));
    renderer.SetScissor({x, y, static_cast<std::int32_t>(std::ceil(bounds.max.x)) - x,
                         static_cast<std::int32_t>(std::ceil(viewSize.y - bounds.min.y)) - y});
}
void Fill(Renderer2D& renderer, UiBounds bounds, Color color) {
    if (!bounds.IsEmpty())
        renderer.DrawQuad(bounds.GetCenter(), bounds.GetSize(), color);
}
// With the style's texture, nine-sliced, or flat without one.
void Fill(Renderer2D& renderer, UiBounds bounds, Color color, const UiControlStyle& style, float scale) {
    if (!style.texture) {
        Fill(renderer, bounds, color);
        return;
    }
    const Vector2 size{static_cast<float>(style.texture->GetWidth()), static_cast<float>(style.texture->GetHeight())};
    for (const auto& quad : BuildUiNineSlice(bounds, size, style.borders, scale))
        renderer.DrawQuad(quad.bounds.GetCenter(), quad.bounds.GetSize(), *style.texture, color, quad.region);
}
std::size_t Previous(std::string_view text, std::size_t caret) {
    if (caret == 0)
        return 0;
    --caret;
    while (caret && (static_cast<unsigned char>(text[caret]) & 0xC0) == 0x80)
        --caret;
    return caret;
}
std::size_t Next(std::string_view text, std::size_t caret) {
    if (caret >= text.size())
        return text.size();
    ++caret;
    while (caret < text.size() && (static_cast<unsigned char>(text[caret]) & 0xC0) == 0x80)
        ++caret;
    return caret;
}
std::string CleanText(std::string_view text, int maxLength) {
    std::string result;
    int count = 0;
    for (const auto point : DecodeUtf8(text)) {
        if (point < 0x20 || point == 0x7F)
            continue;
        if (count++ >= std::max(0, maxLength))
            break;
        if (point < 0x80)
            result.push_back(static_cast<char>(point));
        else if (point < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (point >> 6)));
            result.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        } else if (point < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (point >> 12)));
            result.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (point >> 18)));
            result.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        }
    }
    return result;
}
} // namespace

Color UiControlStyle::GetColor(UiControlState state) const {
    switch (state) {
    case UiControlState::Hovered:
        return hovered;
    case UiControlState::Pressed:
        return pressed;
    case UiControlState::Focused:
        return focused;
    case UiControlState::Disabled:
        return disabled;
    default:
        return normal;
    }
}
void UiControlStyle::Serialize(Archive& archive) {
    archive.Field("normal", normal);
    archive.Field("hovered", hovered);
    archive.Field("pressed", pressed);
    archive.Field("focused", focused);
    archive.Field("disabled", disabled);
    archive.Field("accent", accent);
    archive.Field("texture", texture, {.tooltip = "Drawn tinted by the state's color instead of a flat rectangle"});
    archive.Field("borders", borders, {.tooltip = "Its nine-slice borders: left, bottom, right, top, in its pixels"});
}
const UiLayoutEntry* UiWidget::GetLayout() const {
    const auto* system = GetScene().GetSystem<UiSystem>();
    return system ? system->Find(GetEntity().GetId()) : nullptr;
}
void UiWidget::HandleUiEvent(const UiEvent& event) {
    const Detail::DeferRemovalsScope defer(GetScene());
    if (IsActiveAndEnabled() && m_interactable) {
        RunGuarded("OnUiEvent", [&] { OnUiEvent(event); });
    }
}
void UiWidget::OnUiEvent(const UiEvent&) {
}
void UiWidget::SerializeWidget(Archive& archive) {
    archive.Field("interactable", m_interactable);
}
void UiWidget::OnDrawUi(Renderer2D& renderer, Vector2 viewSize) {
    if (const auto* entry = GetLayout()) {
        if (IntersectUiBounds(entry->bounds, entry->clip).IsEmpty())
            return;
        Clip(renderer, entry->clip, viewSize);
        DrawWidget(renderer, *entry);
    }
}

void UiImage::DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) {
    if (!texture) {
        Fill(renderer, entry.bounds, tint);
        return;
    }
    const Vector2 textureSize{static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight())};
    if (m_cachedBounds != entry.bounds || m_cachedTextureSize != textureSize || m_cachedBorders != borders ||
        m_cachedScale != entry.scale) {
        m_quads = BuildUiNineSlice(entry.bounds, textureSize, borders, entry.scale);
        m_cachedBounds = entry.bounds;
        m_cachedTextureSize = textureSize;
        m_cachedBorders = borders;
        m_cachedScale = entry.scale;
    }
    for (const auto& quad : m_quads)
        renderer.DrawQuad(quad.bounds.GetCenter(), quad.bounds.GetSize(), *texture, tint, quad.region);
}
void UiImage::Serialize(Archive& archive) {
    archive.Field("texture", texture);
    archive.Field("tint", tint);
    archive.Field("borders", borders);
}

Vector2 UiText::GetPreferredSize() const {
    if (m_preferredFont != font || m_preferredText != text || m_preferredFontSize != fontSize) {
        m_preferredSize = font ? font->MeasureText(text) * (Positive(fontSize, 20) / font->GetPixelSize()) : Vector2{};
        m_preferredFont = font;
        m_preferredText = text;
        m_preferredFontSize = fontSize;
    }
    return m_preferredSize;
}
void UiText::DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) {
    if (!font)
        return;
    const float scale = Positive(fontSize, 20) / font->GetPixelSize() * entry.scale;
    if (scale <= 0)
        return;
    const Vector2 box = entry.bounds.GetSize() / scale;
    if (m_cachedFont != font || m_cachedText != text || m_cachedBox != box || m_cachedSettings != settings) {
        m_geometry = LayoutUiText(font->GetAtlas(), text, box, settings);
        m_cachedFont = font;
        m_cachedText = text;
        m_cachedBox = box;
        m_cachedSettings = settings;
    }
    if (settings.overflow != UiTextOverflow::Overflow)
        Clip(renderer, IntersectUiBounds(entry.clip, entry.bounds), GetScene().GetSystem<UiSystem>()->GetViewSize());
    renderer.DrawGlyphs(*font, m_geometry.glyphs, {entry.bounds.min.x, entry.bounds.max.y}, color, scale);
}
void UiText::Serialize(Archive& archive) {
    archive.Field("font", font);
    archive.Field("text", text);
    archive.Field("fontSize", fontSize, {.min = 1, .max = 1000});
    archive.Field("color", color);
    archive.Field("layout", settings);
}

void UiButton::DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) {
    Fill(renderer, entry.bounds, style.GetColor(GetUiState()), style, entry.scale);
}
void UiButton::OnUiEvent(const UiEvent& event) {
    if (event.type == UiEventType::Activate)
        OnClick.Emit();
}
void UiButton::Serialize(Archive& archive) {
    SerializeWidget(archive);
    archive.Field("style", style);
}

void UiToggle::SetValue(bool value, bool notify) {
    if (m_value == value)
        return;
    m_value = value;
    if (notify)
        Notify([&] { OnValueChanged.Emit(m_value); });
}
void UiToggle::DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) {
    Fill(renderer, entry.bounds, style.GetColor(GetUiState()), style, entry.scale);
    if (m_value) {
        const Vector2 inset = entry.bounds.GetSize() * 0.2f;
        Fill(renderer, {entry.bounds.min + inset, entry.bounds.max - inset}, style.accent, style, entry.scale);
    }
}
void UiToggle::OnUiEvent(const UiEvent& event) {
    if (event.type == UiEventType::Activate)
        SetValue(!m_value, true);
}
void UiToggle::Serialize(Archive& archive) {
    SerializeWidget(archive);
    archive.Field("value", m_value);
    archive.Field("style", style);
}

void UiSlider::SetValue(float value, bool notify) {
    const float low = std::isfinite(minimum) ? minimum : 0;
    const float high = std::isfinite(maximum) ? std::max(low, maximum) : low;
    value = std::clamp(std::isfinite(value) ? value : low, low, high);
    if (m_value == value)
        return;
    m_value = value;
    if (notify)
        Notify([&] { OnValueChanged.Emit(m_value); });
}
float UiSlider::GetFraction() const {
    return maximum > minimum ? std::clamp((m_value - minimum) / (maximum - minimum), 0.0f, 1.0f) : 0;
}
void UiSlider::DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) {
    Fill(renderer, entry.bounds, style.GetColor(GetUiState()), style, entry.scale);
    UiBounds fill = entry.bounds;
    if (vertical)
        fill.max.y = fill.min.y + fill.GetSize().y * GetFraction();
    else
        fill.max.x = fill.min.x + fill.GetSize().x * GetFraction();
    Fill(renderer, fill, style.accent, style, entry.scale);
}
void UiSlider::OnUiEvent(const UiEvent& event) {
    if (event.type == UiEventType::Adjust)
        SetValue(m_value + event.value * Positive(step, 0.05f), true);
    if (event.type == UiEventType::PointerDown ||
        (event.type == UiEventType::PointerMove && GetUiState() == UiControlState::Pressed)) {
        if (const auto* entry = GetLayout()) {
            const float length = vertical ? entry->bounds.GetSize().y : entry->bounds.GetSize().x;
            const float offset =
                vertical ? event.position.y - entry->bounds.min.y : event.position.x - entry->bounds.min.x;
            if (length > 0)
                SetValue(minimum + (maximum - minimum) * offset / length, true);
        }
    }
}
void UiSlider::Serialize(Archive& archive) {
    SerializeWidget(archive);
    archive.Field("minimum", minimum);
    archive.Field("maximum", maximum);
    archive.Field("value", m_value);
    archive.Field("step", step);
    archive.Field("vertical", vertical);
    archive.Field("style", style);
    SetValue(m_value);
}

void UiInputField::SetText(std::string text, bool notify) {
    text = CleanText(text, maxLength);
    if (m_text == text) {
        m_caret = std::min(m_caret, m_text.size());
        return;
    }
    m_text = std::move(text);
    m_caret = m_text.size();
    if (notify)
        Notify([&] { OnValueChanged.Emit(m_text); });
}
void UiInputField::OnUiEvent(const UiEvent& event) {
    if (event.type == UiEventType::Submit) {
        OnSubmit.Emit(m_text);
        return;
    }
    if (event.type == UiEventType::Left) {
        m_caret = Previous(m_text, m_caret);
        return;
    }
    if (event.type == UiEventType::Right) {
        m_caret = Next(m_text, m_caret);
        return;
    }
    if (event.type == UiEventType::Home) {
        m_caret = 0;
        return;
    }
    if (event.type == UiEventType::End) {
        m_caret = m_text.size();
        return;
    }
    std::string changed = m_text;
    std::size_t caret = m_caret;
    if (event.type == UiEventType::Text) {
        const int remaining = std::max(0, maxLength - static_cast<int>(DecodeUtf8(m_text).size()));
        const auto inserted = CleanText(event.text, remaining);
        changed.insert(caret, inserted);
        caret += inserted.size();
    } else if (event.type == UiEventType::Backspace && caret) {
        const auto start = Previous(changed, caret);
        changed.erase(start, caret - start);
        caret = start;
    } else if (event.type == UiEventType::Delete) {
        changed.erase(caret, Next(changed, caret) - caret);
    } else
        return;
    SetText(std::move(changed), true);
    m_caret = caret;
}
void UiInputField::DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) {
    Fill(renderer, entry.bounds, style.GetColor(GetUiState()), style, entry.scale);
    if (!font)
        return;
    const float scale = Positive(fontSize, 20) / font->GetPixelSize() * entry.scale;
    if (scale <= 0)
        return;
    const UiBounds bounds{entry.bounds.min + Vector2{4, 2} * entry.scale,
                          entry.bounds.max - Vector2{4, 2} * entry.scale};
    const Vector2 box = bounds.GetSize() / scale;
    const std::string& text = m_text.empty() ? placeholder : m_text;
    if (m_cachedText != text || m_cachedFont != font || m_cachedBox != box) {
        m_geometry = LayoutUiText(font->GetAtlas(), text, box, {.overflow = UiTextOverflow::Overflow, .wrap = false});
        m_cachedText = text;
        m_cachedFont = font;
        m_cachedBox = box;
    }
    const float caretX = font->MeasureText(std::string_view(m_text).substr(0, m_caret)).x * scale;
    const float shift = std::max(0.0f, caretX - bounds.GetSize().x + 2 * entry.scale);
    Clip(renderer, IntersectUiBounds(entry.clip, bounds), GetScene().GetSystem<UiSystem>()->GetViewSize());
    renderer.DrawGlyphs(*font, m_geometry.glyphs, {bounds.min.x - shift, bounds.max.y},
                        m_text.empty() ? Color{textColor.r, textColor.g, textColor.b, textColor.a * 0.5f} : textColor,
                        scale);
    if (GetUiState() == UiControlState::Focused || GetUiState() == UiControlState::Pressed) {
        Fill(renderer,
             {{bounds.min.x + caretX - shift, bounds.min.y},
              {bounds.min.x + caretX - shift + entry.scale, bounds.max.y}},
             textColor);
    }
}
void UiInputField::Serialize(Archive& archive) {
    SerializeWidget(archive);
    archive.Field("font", font);
    archive.Field("fontSize", fontSize);
    archive.Field("textColor", textColor);
    archive.Field("placeholder", placeholder);
    archive.Field("maxLength", maxLength);
    archive.Field("text", m_text);
    archive.Field("style", style);
    m_text = CleanText(m_text, maxLength);
    m_caret = std::min(m_caret, m_text.size());
}

void UiScrollView::SetOffset(Vector2 offset, bool notify) {
    Vector2 viewport{};
    if (const auto* entry = GetLayout())
        viewport = entry->bounds.GetSize() / entry->scale;
    offset.x =
        horizontal ? std::clamp(Positive(offset.x), 0.0f, std::max(0.0f, Positive(contentSize.x) - viewport.x)) : 0;
    offset.y =
        vertical ? std::clamp(Positive(offset.y), 0.0f, std::max(0.0f, Positive(contentSize.y) - viewport.y)) : 0;
    if (m_offset == offset)
        return;
    m_offset = offset;
    if (notify)
        Notify([&] { OnValueChanged.Emit(m_offset); });
}
UiBounds UiScrollView::GetContentBounds(UiBounds viewport, float scale) const {
    const Vector2 size{std::max(viewport.GetSize().x, Positive(contentSize.x) * scale),
                       std::max(viewport.GetSize().y, Positive(contentSize.y) * scale)};
    const Vector2 min{viewport.min.x - m_offset.x * scale, viewport.max.y - size.y + m_offset.y * scale};
    return {min, min + size};
}
void UiScrollView::DrawWidget(Renderer2D& renderer, const UiLayoutEntry& entry) {
    Fill(renderer, entry.bounds, style.GetColor(GetUiState()), style, entry.scale);
}
void UiScrollView::OnUiEvent(const UiEvent& event) {
    if (event.type == UiEventType::Scroll || event.type == UiEventType::Adjust)
        SetOffset(m_offset + Vector2{horizontal ? -event.position.x : 0, vertical ? -event.value : 0} *
                                 Positive(scrollSpeed, 40),
                  true);
}
void UiScrollView::Serialize(Archive& archive) {
    SerializeWidget(archive);
    archive.Field("contentSize", contentSize);
    archive.Field("offset", m_offset);
    archive.Field("horizontal", horizontal);
    archive.Field("vertical", vertical);
    archive.Field("scrollSpeed", scrollSpeed);
    archive.Field("style", style);
}

} // namespace Spot
