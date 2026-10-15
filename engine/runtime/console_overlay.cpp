#include "runtime/console_overlay.h"

#include "core/console.h"
#include "graphics/font.h"
#include "graphics/renderer_2d.h"
#include "runtime/console_input.h"
#include "runtime/runtime_stats.h"

#include <algorithm>

namespace Spot {
namespace {

std::string FitLine(const Font& font, std::string_view text, float width, float scale) {
    std::string line(text);
    while (!line.empty() && font.MeasureText(line).x * scale > width) {
        auto last = line.size() - 1;
        while (last > 0 && (static_cast<unsigned char>(line[last]) & 0xC0) == 0x80) {
            --last;
        }
        line.resize(last);
    }
    return line;
}

} // namespace

void DrawConsoleOverlay(const Console& console, const ConsoleInput& input, const Font& font, Renderer2D& renderer,
                        Vector2 size) {
    if (!input.IsOpen() || size.x <= 24.0f || size.y <= 64.0f) {
        return;
    }
    constexpr float Scale = 0.8f;
    const float line = std::max(12.0f, font.GetLineHeight() * Scale);
    const float height = std::min(size.y, std::max(line * 5.0f, size.y * 0.45f));
    const float bottom = size.y - height;
    renderer.DrawQuad({size.x * 0.5f, size.y - height * 0.5f}, {size.x, height}, {0.015f, 0.02f, 0.03f, 0.95f});
    renderer.DrawString(font, "Console | Enter: run | Tab: complete | Up/Down: history | Esc: close",
                        {12.0f, size.y - 10.0f}, {0.55f, 0.75f, 1.0f, 1.0f}, Scale);
    renderer.DrawString(font, FitLine(font, "> " + input.GetLine() + "_", size.x - 24.0f, Scale),
                        {12.0f, bottom + line + 8.0f}, Color::White, Scale);
    std::vector<std::pair<std::string_view, bool>> rows;
    for (const auto& result : console.GetOutput()) {
        auto text = std::string_view(result.message);
        while (!text.empty()) {
            const auto end = text.find('\n');
            rows.emplace_back(text.substr(0, end), result.success);
            if (end == std::string_view::npos) {
                break;
            }
            text.remove_prefix(end + 1);
        }
    }
    const auto count = static_cast<std::size_t>(std::max(0.0f, (height - line * 3.0f - 16.0f) / line));
    const auto first = rows.size() > count ? rows.size() - count : 0;
    float y = size.y - line - 14.0f;
    for (std::size_t i = first; i < rows.size(); ++i) {
        renderer.DrawString(font, FitLine(font, rows[i].first, size.x - 24.0f, Scale), {12.0f, y},
                            rows[i].second ? Color{0.8f, 0.9f, 0.8f, 1.0f} : Color{1.0f, 0.35f, 0.3f, 1.0f}, Scale);
        y -= line;
    }
}

void DrawStatsOverlay(const RuntimeStats& stats, const Font& font, Renderer2D& renderer, Vector2 size) {
    constexpr float Scale = 0.8f;
    const auto text = stats.Format();
    const auto measured = font.MeasureText(text) * Scale;
    const float width = std::min(size.x, measured.x + 24.0f);
    const float height = measured.y + 20.0f;
    const float x = std::max(0.0f, size.x - width);
    renderer.DrawQuad({x + width * 0.5f, size.y - height * 0.5f}, {width, height}, {0.015f, 0.02f, 0.03f, 0.85f});
    renderer.DrawString(font, text, {x + 12.0f, size.y - 10.0f}, {0.8f, 0.95f, 0.8f, 1.0f}, Scale);
}

} // namespace Spot
