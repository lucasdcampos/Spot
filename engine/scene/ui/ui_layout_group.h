#pragma once

#include "scene/component.h"
#include "scene/entity.h"
#include "scene/ui/ui_layout.h"

#include <span>
#include <vector>

namespace Spot {

enum class UiLayoutDirection { Horizontal, Vertical, Grid };
enum class UiLayoutAlignment { Start, Center, End };

struct UiLayoutGroupSettings {
    UiLayoutDirection direction = UiLayoutDirection::Vertical;
    Vector4 padding = {4, 4, 4, 4};
    Vector2 spacing = {4, 4};
    Vector2 cellSize = {100, 30};
    int columns = 2;
    UiLayoutAlignment alignment = UiLayoutAlignment::Start;
    bool expandWidth = false;
    bool expandHeight = false;
    void Serialize(Archive& archive);
};

// Horizontal order is left to right; vertical and grid order starts at the top-left.
Vector2 MeasureUiLayoutGroup(const UiLayoutGroupSettings& settings, std::span<const Vector2> sizes);
std::vector<UiBounds> ArrangeUiLayoutGroup(const UiLayoutGroupSettings& settings, UiBounds bounds,
                                           std::span<const Vector2> sizes);
void ArrangeUiLayoutGroup(const UiLayoutGroupSettings& settings, UiBounds bounds, std::span<const Vector2> sizes,
                          std::vector<UiBounds>& result);

class UiLayoutGroup final : public Component {
public:
    UiLayoutGroupSettings settings;

protected:
    void Serialize(Archive& archive) override { settings.Serialize(archive); }

private:
    friend class UiSystem;
    std::vector<EntityId> m_children;
    std::vector<Vector2> m_sizes;
    std::vector<UiBounds> m_bounds;
};

class UiSizeFitter final : public Component {
public:
    bool fitWidth = true;
    bool fitHeight = true;

protected:
    void Serialize(Archive& archive) override;
};

} // namespace Spot
