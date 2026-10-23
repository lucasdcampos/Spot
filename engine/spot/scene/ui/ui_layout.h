#pragma once

#include "spot/core/vector.h"
#include "spot/platform/keys.h"

namespace Spot {

struct UiBounds {
    Vector2 min;
    Vector2 max;
    Vector2 GetSize() const { return max - min; }
    Vector2 GetCenter() const { return (min + max) * 0.5f; }
    bool Contains(Vector2 point) const;
    bool IsEmpty() const;
    friend bool operator==(const UiBounds&, const UiBounds&) = default;
};

UiBounds IntersectUiBounds(UiBounds first, UiBounds second);

enum class UiScaleMode { ConstantPixels, ReferenceResolution };

struct UiCanvasSettings {
    UiScaleMode scaleMode = UiScaleMode::ConstantPixels;
    Vector2 referenceResolution = {1280, 720};
    float scale = 1.0f;
    int order = 0;
    bool modal = false;
    Key dismissKey = Key::Escape;
    void Serialize(class Archive& archive);
};

struct UiRectLayout {
    // Coordinates grow right and up. Equal anchors give a fixed size; separated anchors stretch it.
    Vector2 anchorMin = {0.5f, 0.5f};
    Vector2 anchorMax = {0.5f, 0.5f};
    Vector2 pivot = {0.5f, 0.5f};
    Vector2 offset = {};
    Vector2 sizeDelta = {100, 30};
    void Serialize(class Archive& archive);
};

float ComputeUiScale(const UiCanvasSettings& settings, Vector2 viewSize);
UiBounds ComputeUiRect(const UiRectLayout& layout, UiBounds parent);

} // namespace Spot
