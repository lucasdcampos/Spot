#pragma once

#include "spot/math/vector.h"

namespace Spot {

class Console;
class ConsoleInput;
class Font;
class Renderer2D;
struct RuntimeStats;

// Draw inside a Renderer2D pass whose camera covers [0, viewSize] with Y up, after the game's UI.
void DrawConsoleOverlay(const Console& console, const ConsoleInput& input, const Font& font, Renderer2D& renderer,
                        Vector2 viewSize);
void DrawStatsOverlay(const RuntimeStats& stats, const Font& font, Renderer2D& renderer, Vector2 viewSize);

} // namespace Spot
