#include "spot/scene/ui/ui_layout_group.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/io/archive.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Spot {
namespace {
float Positive(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0;
}
Vector2 Positive(Vector2 value) {
    return {Positive(value.x), Positive(value.y)};
}
Vector4 Padding(const UiLayoutGroupSettings& settings) {
    return {Positive(settings.padding.x), Positive(settings.padding.y), Positive(settings.padding.z),
            Positive(settings.padding.w)};
}
float Align(UiLayoutAlignment alignment, float space) {
    space = std::max(0.0f, space);
    return alignment == UiLayoutAlignment::Center ? space * 0.5f : alignment == UiLayoutAlignment::End ? space : 0;
}
} // namespace

Vector2 MeasureUiLayoutGroup(const UiLayoutGroupSettings& settings, std::span<const Vector2> sizes) {
    const auto padding = Padding(settings);
    Vector2 total{padding.x + padding.z, padding.y + padding.w};
    if (sizes.empty())
        return total;
    Vector2 content;
    if (settings.direction == UiLayoutDirection::Grid) {
        const auto columns = std::clamp(settings.columns, 1, 100000);
        const auto count = static_cast<int>(sizes.size());
        const auto usedColumns = std::min(columns, count);
        const auto rows = (count + columns - 1) / columns;
        content = {Positive(settings.cellSize.x) * static_cast<float>(usedColumns) +
                       Positive(settings.spacing.x) * static_cast<float>(usedColumns - 1),
                   Positive(settings.cellSize.y) * static_cast<float>(rows) +
                       Positive(settings.spacing.y) * static_cast<float>(rows - 1)};
    } else {
        for (auto size : sizes) {
            size = Positive(size);
            if (settings.direction == UiLayoutDirection::Horizontal) {
                content.x += size.x;
                content.y = std::max(content.y, size.y);
            } else {
                content.y += size.y;
                content.x = std::max(content.x, size.x);
            }
        }
        if (settings.direction == UiLayoutDirection::Horizontal)
            content.x += Positive(settings.spacing.x) * static_cast<float>(sizes.size() - 1);
        else
            content.y += Positive(settings.spacing.y) * static_cast<float>(sizes.size() - 1);
    }
    return total + content;
}

std::vector<UiBounds> ArrangeUiLayoutGroup(const UiLayoutGroupSettings& settings, UiBounds bounds,
                                           std::span<const Vector2> sizes) {
    std::vector<UiBounds> result;
    ArrangeUiLayoutGroup(settings, bounds, sizes, result);
    return result;
}

void ArrangeUiLayoutGroup(const UiLayoutGroupSettings& settings, UiBounds bounds, std::span<const Vector2> sizes,
                          std::vector<UiBounds>& result) {
    result.clear();
    result.reserve(sizes.size());
    if (sizes.empty())
        return;
    const auto padding = Padding(settings);
    const Vector2 innerMin = bounds.min + Vector2{padding.x, padding.y};
    const Vector2 innerMax{std::max(innerMin.x, bounds.max.x - padding.z),
                           std::max(innerMin.y, bounds.max.y - padding.w)};
    const Vector2 available = innerMax - innerMin;
    const Vector2 spacing = Positive(settings.spacing);
    if (settings.direction == UiLayoutDirection::Grid) {
        const auto columns = std::clamp(settings.columns, 1, 100000);
        const auto rows = (static_cast<int>(sizes.size()) + columns - 1) / columns;
        Vector2 cell = Positive(settings.cellSize);
        if (settings.expandWidth)
            cell.x = std::max(0.0f, (available.x - spacing.x * static_cast<float>(columns - 1)) /
                                        static_cast<float>(columns));
        if (settings.expandHeight)
            cell.y =
                std::max(0.0f, (available.y - spacing.y * static_cast<float>(rows - 1)) / static_cast<float>(rows));
        const float offset = Align(settings.alignment, available.x - cell.x * static_cast<float>(columns) -
                                                           spacing.x * static_cast<float>(columns - 1));
        for (std::size_t index = 0; index < sizes.size(); ++index) {
            const auto column = static_cast<int>(index) % columns, row = static_cast<int>(index) / columns;
            const Vector2 min{innerMin.x + offset + static_cast<float>(column) * (cell.x + spacing.x),
                              innerMax.y - cell.y - static_cast<float>(row) * (cell.y + spacing.y)};
            result.push_back({min, min + cell});
        }
        return;
    }
    const bool horizontal = settings.direction == UiLayoutDirection::Horizontal;
    const float count = static_cast<float>(sizes.size());
    const auto preferred =
        MeasureUiLayoutGroup(settings, sizes) - Vector2{padding.x + padding.z, padding.y + padding.w};
    const bool expandMain = horizontal ? settings.expandWidth : settings.expandHeight;
    const float uniform = std::max(
        0.0f, ((horizontal ? available.x : available.y) - (horizontal ? spacing.x : spacing.y) * (count - 1)) / count);
    const float offset =
        expandMain ? 0 : Align(settings.alignment, horizontal ? available.x - preferred.x : available.y - preferred.y);
    float cursor = horizontal ? innerMin.x + offset : innerMax.y - offset;
    for (auto size : sizes) {
        size = Positive(size);
        if (settings.expandWidth)
            size.x = horizontal ? uniform : available.x;
        if (settings.expandHeight)
            size.y = horizontal ? available.y : uniform;
        const Vector2 min =
            horizontal ? Vector2{cursor, innerMax.y - size.y - Align(settings.alignment, available.y - size.y)}
                       : Vector2{innerMin.x + Align(settings.alignment, available.x - size.x), cursor - size.y};
        result.push_back({min, min + size});
        cursor += horizontal ? size.x + spacing.x : -(size.y + spacing.y);
    }
}

void UiLayoutGroupSettings::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 3> Directions{"Horizontal", "Vertical", "Grid"};
    static constexpr std::array<std::string_view, 3> Alignments{"Start", "Center", "End"};
    archive.Field("direction", direction, {.enumNames = Directions});
    archive.Field("padding", padding);
    archive.Field("spacing", spacing);
    archive.Field("cellSize", cellSize);
    archive.Field("columns", columns, {.min = 1, .max = 1000});
    archive.Field("alignment", alignment, {.enumNames = Alignments});
    archive.Field("expandWidth", expandWidth);
    archive.Field("expandHeight", expandHeight);
}
void UiSizeFitter::Serialize(Archive& archive) {
    archive.Field("fitWidth", fitWidth);
    archive.Field("fitHeight", fitHeight);
}

} // namespace Spot
