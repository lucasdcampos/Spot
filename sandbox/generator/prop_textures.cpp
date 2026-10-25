#include "prop_textures.h"

#include "texture_painting.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Generator::Textures {

namespace {

// The round props' ends, as Generator::Shapes lays them out, in the texture's units.
constexpr float CapRadius = 0.24f;
constexpr float TopCapU = 0.25f;
constexpr float BottomCapU = 0.75f;
constexpr float CapV = 0.75f;

float Distance(float u, float v, float cu, float cv) {
    return std::sqrt((u - cu) * (u - cu) + (v - cv) * (v - cv));
}

// How far u is from the center, the shorter way around, as round props' sides wrap.
float AroundFrom(float u, float center) {
    const float d = u - center;
    return d - std::floor(d + 0.5f);
}

float Fraction(float value) {
    return value - std::floor(value);
}

// Paints a prop's textures from what it is at each point, (u, v) from 0 to 1, given how wide a pixel is in those
// units, then weathers them.
template <typename F>
SurfaceTextures PaintProp(std::uint32_t size, F&& surface, std::initializer_list<Weathering> weatherings) {
    return PaintSurfaceTexels(
        size,
        [&](int x, int y, float scale) { return surface(Coordinate(x, scale), Coordinate(y, scale), scale / 256.0f); },
        weatherings);
}

// A nail's domed head, at the distance from its center, in a halo of rust.
void Nail(SurfacePoint& point, float distance, float radius) {
    if (distance > radius * 2.5f) {
        return;
    }
    point.color = Mix(point.color, Rgb{0.3f, 0.17f, 0.09f}, Smooth(radius * 2.5f, radius, distance) * 0.5f);
    if (distance < radius) {
        const float dome = 1.0f - (distance / radius) * (distance / radius);
        point.color = Rgb{0.3f, 0.29f, 0.28f} * (0.7f + dome * 0.5f);
        point.height += 0.5f + dome * 0.8f;
        point.occlusion = 1.0f;
        point.roughness = 0.5f;
        point.metallic = 0.7f;
    }
}

// Lines of print seen from arm's length, from 0 to 1: words of random lengths along the rows of the block, its
// coordinates from 0 to 1 within it.
float Print(float u, float v, int rows, std::uint32_t seed) {
    const float row = v * static_cast<float>(rows);
    const int line = static_cast<int>(std::floor(row));
    const float inRow = row - static_cast<float>(line);
    if (u < 0.0f || v < 0.0f || u > 0.55f + Random(line, 0, seed) * 0.45f || v >= 1.0f || inRow < 0.3f ||
        inRow > 0.72f) {
        return 0.0f;
    }
    const float letters = u * 46.0f;
    const int letter = static_cast<int>(letters);
    const bool space = Random(letter, line, seed + 1) < 0.18f;
    return space || letters - static_cast<float>(letter) > 0.78f ? 0.0f : 1.0f;
}

SurfacePoint CratePoint(float u, float v, float) {
    constexpr float Frame = 0.11f;
    constexpr float Inner = 1.0f - Frame;
    constexpr float BraceHalf = 0.06f;
    constexpr int Planks = 4;
    constexpr float PlankHeight = (Inner - Frame) / Planks;
    // Which board the point is on, how far it is from the board's edges, and how far the board stands out: the
    // frame and the brace lie over the planks.
    int board = 0;
    float fromEdge = 0.0f;
    float lift = 0.0f;
    WoodFigure figure;
    if (v < Frame || v > Inner) {
        board = v < Frame ? 0 : 1;
        const float local = v < Frame ? v : v - Inner;
        fromEdge = std::min({local, Frame - local, u, 1.0f - u});
        figure =
            WoodGrain(u, local + static_cast<float>(board) * 0.37f, 70, 1100 + static_cast<std::uint32_t>(board) * 16);
        lift = 3.0f;
    } else if (u < Frame || u > Inner) {
        board = u < Frame ? 2 : 3;
        const float local = u < Frame ? u : u - Inner;
        fromEdge = std::min({local, Frame - local, v - Frame, Inner - v});
        figure =
            WoodGrain(v, local + static_cast<float>(board) * 0.29f, 70, 1100 + static_cast<std::uint32_t>(board) * 16);
        lift = 3.0f;
    } else {
        const float across = (u - v) * 0.70711f;
        const float inside = std::min({u - Frame, Inner - u, v - Frame, Inner - v});
        if (std::abs(across) < BraceHalf) {
            board = 4;
            fromEdge = std::min(BraceHalf - std::abs(across), inside);
            figure = WoodGrain((u + v) * 0.70711f, across + 0.5f, 70, 1180);
            lift = 2.6f;
        } else {
            const float row = (v - Frame) / PlankHeight;
            const int plank = std::min(static_cast<int>(row), Planks - 1);
            const float local = row - static_cast<float>(plank);
            board = 5 + plank;
            fromEdge = std::min(std::min(local, 1.0f - local) * PlankHeight, inside);
            figure =
                WoodGrain(u, v + static_cast<float>(plank) * 0.21f, 64, 1200 + static_cast<std::uint32_t>(plank) * 16);
        }
    }
    // The frame's boards are of a darker batch than the planks.
    const float tint = (0.86f + Random(board, 0, 1101) * 0.24f) * (board < 5 ? 0.9f : 1.0f);
    const float grime = Smooth(0.55f, 1.0f, v) * 0.12f + Smooth(0.6f, 0.8f, Noise(u, v, 4, 4, 4, 1102)) * 0.08f;
    Rgb color = Mix(Rgb{0.76f, 0.58f, 0.36f}, Rgb{0.45f, 0.29f, 0.15f}, figure.tone);
    color = color * (tint * (1.0f - figure.pores * 0.22f) * (1.0f - grime));
    SurfacePoint point{.color = color,
                       .height =
                           lift + Smooth(0.0f, 0.012f, fromEdge) * 1.5f + figure.tone * 0.3f - figure.pores * 0.35f,
                       .roughness = 0.7f + figure.pores * 0.15f + grime * 0.3f};
    if (board >= 5 && fromEdge < 0.003f) {
        point.color = Rgb{0.08f, 0.06f, 0.04f};
        point.height = -2.0f;
        point.occlusion = 0.35f;
        point.roughness = 0.95f;
    }
    constexpr float NailRadius = 0.0065f;
    for (const float x : {Frame * 0.5f, 1.0f - Frame * 0.5f}) {
        for (const float y : {Frame * 0.5f, 1.0f - Frame * 0.5f}) {
            Nail(point, Distance(u, v, x - 0.022f, y), NailRadius);
            Nail(point, Distance(u, v, x + 0.022f, y), NailRadius);
        }
        // Through the frame's sides into the planks' ends.
        for (int plank = 0; plank < Planks; ++plank) {
            Nail(point, Distance(u, v, x, Frame + (static_cast<float>(plank) + 0.5f) * PlankHeight), NailRadius);
        }
    }
    for (const float end : {Frame + 0.07f, Inner - 0.07f}) {
        for (const float side : {-0.025f, 0.025f}) {
            Nail(point, Distance(u, v, end + side, end - side), NailRadius);
        }
    }
    return point;
}

// Pine along u, tiling, with two knots and the faint arcs of the saw.
SurfacePoint PlankPoint(float u, float v, float) {
    const WoodFigure figure = WoodGrain(u, v, 40, 1300, {{0.27f, 0.13f, 0.035f, 0.014f}, {0.71f, 0.2f, 0.05f, 0.018f}});
    const float saw = Ridges(u + v * v * 0.6f, v, 30, 2, 1, 1301);
    Rgb color = Mix(Rgb{0.82f, 0.64f, 0.42f}, Rgb{0.5f, 0.32f, 0.17f}, figure.tone);
    color = color * ((1.0f - figure.pores * 0.2f) * (0.96f + saw * 0.05f));
    return {.color = color,
            .height = figure.tone * 0.4f - figure.pores * 0.3f + saw * 0.25f,
            .roughness = 0.68f + figure.pores * 0.15f - figure.tone * 0.05f};
}

// A painted steel drum: its paint ripples and shades, chips and scratches through to steel or rust where it rolls on
// its hoops and stands on its chimes, and rust runs down from the chips.
struct Drum {
    Rgb coat;
    std::uint32_t seed = 0;
    bool hazard = false;
};

SurfacePoint DrumPoint(float u, float v, const Drum& drum) {
    const std::uint32_t seed = drum.seed;
    const float peel = GradientNoise(u * 240.0f, v * 240.0f, 240, 240, seed);
    const float shade = Noise(u, v, 6, 6, 4, seed + 1);
    Rgb coat = drum.coat;
    float handled = 0.0f;
    float lift = 0.0f;
    float grime = 0.0f;
    float paper = 0.0f;
    if (v < 0.5f) {
        const float s = v / 0.5f;
        for (const float hoop : {1.0f / 3.0f, 2.0f / 3.0f}) {
            handled = std::max(handled, Smooth(0.035f, 0.0f, std::abs(s - hoop)));
        }
        handled = std::max(handled, Smooth(0.06f, 0.0f, std::min(s, 1.0f - s)));
        grime = Smooth(0.7f, 1.0f, s) * 0.3f;
        if (drum.hazard) {
            if (s > 0.43f && s < 0.57f) {
                // Stripes around the middle, a whole number of them so they meet around the drum.
                const bool yellow = Fraction(u * 24.0f + v * 6.0f) < 0.5f;
                coat = yellow ? Rgb{0.93f, 0.72f, 0.1f} : Rgb{0.07f, 0.07f, 0.07f};
                if (s < 0.44f || s > 0.56f) {
                    coat = Rgb{0.07f, 0.07f, 0.07f};
                }
            }
            // A diamond warning of fire on two sides: a white border around red, and a white flame.
            for (const float center : {0.25f, 0.75f}) {
                const float du = AroundFrom(u, center);
                const float dv = v - 0.085f;
                const float diamond = std::abs(du) + std::abs(dv);
                if (diamond < 0.064f) {
                    coat = diamond > 0.055f ? Rgb{0.92f, 0.9f, 0.86f} : Rgb{0.78f, 0.1f, 0.07f};
                    if (diamond > 0.062f) {
                        coat = Rgb{0.06f, 0.06f, 0.06f};
                    }
                    // The flame: a round body tapering to a point above it, over a bar.
                    const float body = std::sqrt(du * du + (dv - 0.006f) * (dv - 0.006f));
                    const float rise = (0.006f - dv) / 0.04f;
                    const bool flame = body < 0.014f || (rise > 0.0f && rise < 1.0f &&
                                                         std::abs(du + rise * rise * 0.006f) < 0.014f * (1.0f - rise));
                    const bool bar = std::abs(dv - 0.028f) < 0.003f && std::abs(du) < 0.02f;
                    if (flame || bar) {
                        coat = Rgb{0.92f, 0.9f, 0.86f};
                    }
                }
            }
            // Soot from fires nearby, around the top.
            grime += Smooth(0.3f, 0.0f, s) * Noise(u, v, 8, 4, 4, seed + 9) * 0.6f;
        } else {
            // A paper label between the hoops, printed, yellowed and torn at one corner.
            const float lu = (AroundFrom(u, 0.72f) + 0.12f) / 0.24f;
            const float lv = (v - 0.205f) / 0.09f;
            const bool torn = lu + (1.0f - lv) < 0.25f + Noise(u, v, 40, 40, 2, seed + 10) * 0.1f;
            if (lu > 0.0f && lu < 1.0f && lv > 0.0f && lv < 1.0f && !torn) {
                paper = 1.0f;
                const float yellowed = Noise(u, v, 12, 12, 3, seed + 11);
                coat = Rgb{0.9f, 0.88f, 0.8f} * (0.86f + yellowed * 0.14f);
                if (lv < 0.22f) {
                    coat = Rgb{0.85f, 0.42f, 0.08f};
                }
                if (Print((lu - 0.08f) / 0.84f, (lv - 0.3f) / 0.62f, 6, seed + 12) > 0.5f) {
                    coat = Rgb{0.12f, 0.12f, 0.13f};
                }
                lift = 0.25f;
            }
        }
    } else {
        // The lids: two rings pressed into them, the chime rolled around their edge, and bungs in the top one.
        const bool top = u < 0.5f;
        const float cu = top ? TopCapU : BottomCapU;
        const float radius = Distance(u, v, cu, CapV) / CapRadius;
        lift = -Smooth(0.035f, 0.0f, std::abs(radius - 0.52f)) * 0.8f + Smooth(0.05f, 0.0f, std::abs(radius - 0.8f));
        if (radius > 0.955f) {
            handled = 1.0f;
            lift += 1.5f;
        }
        grime = top ? 0.1f : 0.35f;
        if (top) {
            const struct {
                float u;
                float v;
                float radius;
            } bungs[] = {{cu + CapRadius * 0.44f, CapV - CapRadius * 0.44f, 0.028f},
                         {cu - CapRadius * 0.5f, CapV + CapRadius * 0.5f, 0.016f}};
            for (const auto& bung : bungs) {
                const float distance = Distance(u, v, bung.u, bung.v) / bung.radius;
                if (distance < 1.0f) {
                    // A raised flange around a plug with a slot across it.
                    lift += distance > 0.7f ? 1.4f : 0.6f;
                    if (distance < 0.7f && std::abs(v - bung.v) < bung.radius * 0.12f) {
                        lift -= 0.8f;
                    }
                    handled = std::max(handled, distance > 0.7f ? 0.6f : 0.3f);
                }
                grime += Smooth(2.2f, 1.0f, distance) * 0.3f;
            }
        }
    }
    const float scratched = Smooth(0.5f, 0.7f, Noise(u, v, 5, 5, 2, seed + 2));
    const float scratches = std::max(Smooth(0.975f, 0.995f, Ridges(u + v, u - v, 4, 52, 1, seed + 3)),
                                     Smooth(0.98f, 0.996f, Ridges(u, v, 3, 60, 1, seed + 4))) *
                            scratched;
    // Small chips, most of them where it is handled.
    const float chips =
        Smooth(0.88f, 0.9f,
               Noise(u, v, 18, 18, 4, seed + 5) * 0.85f + handled * 0.3f + Noise(u, v, 4, 4, 2, seed + 13) * 0.15f) *
        (1.0f - paper);
    const float rusty = Smooth(0.45f, 0.6f, Noise(u, v, 7, 7, 3, seed + 6));
    const float runs =
        Smooth(0.62f, 0.9f, Noise(u, v, 48, 3, 2, seed + 7)) * Smooth(0.5f, 0.7f, Noise(u, v, 6, 2, 3, seed + 8));
    Rgb color = coat * (0.92f + shade * 0.1f + peel * 0.03f);
    color = Mix(color, Rgb{0.38f, 0.2f, 0.1f}, v < 0.5f ? runs * 0.35f * (1.0f - paper) : 0.0f);
    SurfacePoint point{.color = color * (1.0f - grime),
                       .height = lift + peel * 0.3f + shade * 0.4f,
                       .roughness = paper > 0.0f ? 0.85f : 0.42f + shade * 0.1f + grime * 0.3f};
    const float bare = std::max(chips, scratches * (1.0f - paper));
    if (bare > 0.0f) {
        const Rgb steel = Rgb{0.58f, 0.58f, 0.6f} * (0.85f + peel * 0.2f);
        const Rgb rust = Mix(Rgb{0.5f, 0.25f, 0.1f}, Rgb{0.3f, 0.14f, 0.07f}, peel);
        const float rusted = rusty * chips;
        point.color = Mix(point.color * 0.75f, Mix(steel, rust, rusted), bare);
        point.height -= bare * 0.6f;
        point.roughness += (0.35f + rusted * 0.5f - point.roughness) * bare;
        point.metallic = bare * (1.0f - rusted);
    }
    return point;
}

// Painted steel, rusting through at the beam's edges and around its rivets, and running down from them.
SurfacePoint BeamPoint(float u, float v, float) {
    constexpr float FaceHeight = 0.2f / 0.75f;
    const float peel = GradientNoise(u * 200.0f, v * 200.0f, 200, 200, 1500);
    const float shade = Noise(u, v, 8, 8, 4, 1501);
    const float edges = Smooth(0.02f, 0.0f, std::min(v, std::abs(v - FaceHeight)));
    float lift = peel * 0.3f + shade * 0.4f;
    float rivet = 0.0f;
    float below = 0.0f;
    for (const float row : {0.045f, FaceHeight - 0.045f}) {
        const float du = AroundFrom(u * 8.0f, 0.5f) / 8.0f;
        const float distance = std::sqrt(du * du + (v - row) * (v - row)) / 0.017f;
        if (distance < 1.0f) {
            rivet = 1.0f - distance * distance;
            lift += 1.0f + rivet * 2.0f;
        }
        if (v > row && v < row + 0.1f) {
            below = std::max(below, Smooth(0.012f, 0.004f, std::abs(du)) * Smooth(row + 0.1f, row, v));
        }
    }
    // Rust breaks through mostly along the edges and around the rivets, where the paint is thinnest.
    const float rust = Smooth(0.7f, 0.76f, Noise(u, v, 10, 10, 5, 1502) * 0.85f + edges * 0.4f + rivet * 0.3f);
    const float pits = Smooth(0.5f, 0.85f, GradientNoise(u * 140.0f, v * 140.0f, 140, 140, 1503)) * 0.5f;
    Rgb color = Rgb{0.33f, 0.36f, 0.33f} * (0.9f + shade * 0.12f + peel * 0.04f);
    color = Mix(color, Rgb{0.4f, 0.2f, 0.09f}, below * 0.5f);
    const Rgb rusted = Mix(Rgb{0.52f, 0.27f, 0.11f}, Rgb{0.28f, 0.13f, 0.06f}, pits);
    return {.color = Mix(color, rusted, rust),
            .height = lift + rust * (0.6f - pits * 0.8f),
            .occlusion = 1.0f - pits * rust * 0.2f,
            .roughness = 0.5f + shade * 0.1f + rust * 0.4f};
}

// Vinyl panels in six colors, joined by grooved seams, pebbled, with scuffs and dirt from rolling.
SurfacePoint BallPoint(float u, float v, float) {
    constexpr Rgb Panels[] = {{0.86f, 0.14f, 0.11f}, {0.96f, 0.78f, 0.14f}, {0.14f, 0.42f, 0.86f},
                              {0.18f, 0.7f, 0.28f},  {0.96f, 0.48f, 0.1f},  {0.93f, 0.93f, 0.92f}};
    const float pebble = GradientNoise(u * 300.0f, v * 150.0f, 300, 150, 1600);
    const float dirt = Smooth(0.55f, 0.8f, Noise(u, v, 6, 3, 4, 1601));
    const float scuffs = Smooth(0.97f, 0.99f, Ridges(u + v, u - v, 4, 30, 1, 1602)) * dirt;
    const bool pole = v < 0.1f || v > 0.9f;
    const int panel = std::min(static_cast<int>(u * 6.0f), 5);
    Rgb color = pole ? Rgb{0.94f, 0.94f, 0.93f} : Panels[panel];
    color = color * ((0.94f + pebble * 0.08f) * (1.0f - dirt * 0.18f) * (1.0f + scuffs * 0.3f));
    // The seams, around the poles and between the panels.
    const float seam = std::min(std::abs(v - 0.1f) * 2.0f, std::abs(v - 0.9f) * 2.0f);
    const float between = pole ? 1.0f : std::abs(AroundFrom(u * 6.0f, 0.0f)) / 6.0f;
    const float groove = Smooth(0.006f, 0.002f, std::min(seam, between));
    // The valve, on one panel's middle.
    const float valve = Distance(u * 2.0f, v, 1.0f / 6.0f, 0.5f);
    SurfacePoint point{.color = Mix(color, color * 0.4f, groove),
                       .height = pebble * 0.5f - groove * 1.5f,
                       .occlusion = 1.0f - groove * 0.4f,
                       .roughness = 0.5f + pebble * 0.08f + dirt * 0.25f};
    if (valve < 0.012f) {
        point.color = Rgb{0.15f, 0.15f, 0.15f};
        point.height -= valve < 0.006f ? 1.0f : 0.3f;
        point.roughness = 0.7f;
    }
    return point;
}

// Orange PVC with two reflective bands, scuffed by tires and dirtier toward its base; its base plate black rubber.
SurfacePoint ConePoint(float u, float v, float) {
    const float grain = GradientNoise(u * 260.0f, v * 260.0f, 260, 260, 1700);
    const float dirt = Smooth(0.5f, 0.85f, Noise(u, v, 6, 6, 4, 1701));
    if (v < 0.5f) {
        const float s = v / 0.5f;
        const bool band = (s > 0.344f && s < 0.453f) || (s > 0.594f && s < 0.688f);
        // Faint smears where tires and boots brushed it.
        const float marks =
            Smooth(0.72f, 0.9f, Noise(u, v, 40, 8, 3, 1702)) * Smooth(0.6f, 0.75f, Noise(u, v, 4, 4, 2, 1703));
        const float scuffs = Smooth(0.975f, 0.995f, Ridges(u + v, u - v, 4, 36, 1, 1704));
        const float base = Smooth(0.7f, 1.0f, s);
        SurfacePoint point{.color = Rgb{1.0f, 0.4f, 0.05f} * (0.9f + grain * 0.08f),
                           .height = grain * 0.3f,
                           .roughness = 0.42f + grain * 0.06f};
        if (band) {
            // Prisms in the sheeting, in tiny cells that each catch the light differently.
            const CellSample prisms = Cells(u * 260.0f, v * 260.0f, 260, 1705);
            point.color = Rgb{0.86f, 0.87f, 0.88f} * (0.88f + prisms.id * 0.12f);
            point.height = 0.4f + prisms.id * 0.15f;
            point.roughness = 0.22f + prisms.id * 0.1f;
        }
        point.color = point.color * ((1.0f - marks * 0.3f) * (1.0f - base * dirt * 0.3f) * (1.0f + scuffs * 0.25f));
        point.roughness = std::min(point.roughness + base * dirt * 0.35f + marks * 0.2f, 1.0f);
        return point;
    }
    // The base plate, its underside and its top, which the plate's top face shows.
    return {.color = Rgb{0.07f, 0.07f, 0.075f} * (0.9f + grain * 0.15f) * (1.0f + dirt * 0.5f),
            .height = grain * 0.6f,
            .roughness = 0.82f + dirt * 0.1f};
}

// A tire: blocks of tread in chevrons around its side, worn smooth on top and dusty in the grooves, and on each end
// a sidewall with lettering and ribs around a painted steel wheel with lug nuts and vents.
SurfacePoint WheelPoint(float u, float v, float) {
    const float grain = GradientNoise(u * 300.0f, v * 300.0f, 300, 300, 1800);
    const float dust = Noise(u, v, 8, 8, 3, 1801);
    if (v < 0.5f) {
        const float s = v / 0.5f;
        const float across = std::abs(s - 0.5f);
        const float chevron = Fraction(u * 40.0f + across * 3.0f);
        const bool groove = chevron < 0.28f || across < 0.035f || std::abs(across - 0.3f) < 0.02f;
        const float shoulder = Smooth(0.5f, 0.42f, across);
        if (groove) {
            return {.color = Mix(Rgb{0.06f, 0.06f, 0.06f}, Rgb{0.32f, 0.29f, 0.25f}, dust * 0.5f),
                    .height = -1.5f + shoulder,
                    .occlusion = 0.5f,
                    .roughness = 0.95f};
        }
        return {.color = Rgb{0.085f, 0.085f, 0.09f} * (0.9f + grain * 0.2f),
                .height = 1.5f * shoulder + grain * 0.3f,
                .roughness = 0.7f + grain * 0.1f - shoulder * 0.1f};
    }
    const bool top = u < 0.5f;
    const float cu = top ? TopCapU : BottomCapU;
    const float du = u - cu;
    const float dv = v - CapV;
    const float radius = std::sqrt(du * du + dv * dv) / CapRadius;
    if (radius > 0.58f) {
        // The sidewall: ribs near the rim, a ring of raised lettering, and the tread's shoulder at its edge.
        SurfacePoint point{.color = Rgb{0.08f, 0.08f, 0.085f} * (0.9f + grain * 0.15f),
                           .height = grain * 0.3f + Smooth(1.0f, 0.9f, radius) * 1.5f,
                           .roughness = 0.75f};
        if (radius > 0.6f && radius < 0.66f && Fraction(radius * 160.0f) < 0.5f) {
            point.height += 0.5f;
        }
        if (radius > 0.74f && radius < 0.82f) {
            // Letters as blocks along the ring, in two arcs. How far around is measured without trigonometry, whose
            // results may differ between machines: it is not quite even, which lettering does not show.
            const float slope = dv / (std::abs(du) + std::abs(dv));
            const float around = (du >= 0.0f ? 1.0f - slope : 3.0f + slope) * 0.25f;
            const float arc = Fraction(around * 2.0f);
            const float letters = arc * 40.0f;
            const int letter = static_cast<int>(letters);
            const bool raised = arc > 0.1f && arc < 0.6f && letters - static_cast<float>(letter) < 0.7f &&
                                Random(letter, 0, 1802) > 0.15f &&
                                Random(letter, static_cast<int>((radius - 0.74f) * 40.0f), 1803) > 0.3f;
            if (raised) {
                point.height += 1.0f;
                point.color = point.color * 1.35f;
                point.roughness = 0.6f;
            }
        }
        return point;
    }
    // The wheel: a pressed steel dish painted silver, vents around it, five lug nuts and a domed cap in the middle.
    SurfacePoint point{.color = Rgb{0.68f, 0.69f, 0.7f} * (0.9f + grain * 0.06f) * (1.0f - dust * 0.15f),
                       .height =
                           Smooth(0.58f, 0.5f, radius) * 1.5f - Smooth(0.06f, 0.0f, std::abs(radius - 0.34f)) * 0.8f,
                       .roughness = 0.38f + dust * 0.2f,
                       .metallic = 1.0f};
    if (radius > 0.54f) {
        point.color = Rgb{0.08f, 0.08f, 0.085f};
        point.height = 1.0f;
        point.roughness = 0.6f;
        point.metallic = 0.0f;
        return point;
    }
    constexpr float Vents[8][2] = {{1.0f, 0.0f},  {0.707f, 0.707f},   {0.0f, 1.0f},  {-0.707f, 0.707f},
                                   {-1.0f, 0.0f}, {-0.707f, -0.707f}, {0.0f, -1.0f}, {0.707f, -0.707f}};
    for (const auto& vent : Vents) {
        if (Distance(du, dv, vent[0] * CapRadius * 0.43f, vent[1] * CapRadius * 0.43f) < CapRadius * 0.06f) {
            point.color = Rgb{0.03f, 0.03f, 0.03f};
            point.height = -3.0f;
            point.occlusion = 0.2f;
            point.metallic = 0.0f;
        }
    }
    constexpr float Lugs[5][2] = {
        {0.0f, -1.0f}, {0.951f, -0.309f}, {0.588f, 0.809f}, {-0.588f, 0.809f}, {-0.951f, -0.309f}};
    for (const auto& lug : Lugs) {
        const float distance =
            Distance(du, dv, lug[0] * CapRadius * 0.24f, lug[1] * CapRadius * 0.24f) / (CapRadius * 0.045f);
        if (distance < 1.0f) {
            point.color = Rgb{0.5f, 0.5f, 0.52f};
            point.height += 1.5f + (1.0f - distance * distance);
            point.roughness = 0.3f;
        }
    }
    if (radius < 0.13f) {
        point.height += 1.0f + (1.0f - radius / 0.13f) * 1.5f;
        point.color = Rgb{0.75f, 0.76f, 0.78f};
        point.roughness = 0.22f;
    }
    return point;
}

// A paper target stapled to birch plywood, with a few holes shot through it.
SurfacePoint TargetPoint(float u, float v, float) {
    const float radius = Distance(u, v, 0.5f, 0.5f);
    const float onPaper = std::max(std::abs(u - 0.5f), std::abs(v - 0.5f));
    const float fibers = GradientNoise(u * 300.0f, v * 300.0f, 300, 300, 1900);
    if (onPaper > 0.45f) {
        const WoodFigure figure = WoodGrain(u, v, 36, 1901);
        Rgb color = Mix(Rgb{0.8f, 0.64f, 0.44f}, Rgb{0.56f, 0.4f, 0.24f}, figure.tone);
        return {.color = color * (1.0f - figure.pores * 0.15f),
                .height = figure.tone * 0.3f - figure.pores * 0.3f,
                .roughness = 0.72f + figure.pores * 0.1f};
    }
    Rgb color = Rgb{0.92f, 0.9f, 0.82f} * (0.95f + fibers * 0.06f);
    if (radius < 0.06f) {
        color = Rgb{0.8f, 0.06f, 0.04f};
    } else if (radius < 0.43f && static_cast<int>(radius * 16.0f) % 2 == 0) {
        color = Rgb{0.11f, 0.11f, 0.11f} * (0.95f + fibers * 0.1f);
    }
    // The paper curls up a little at its edges.
    SurfacePoint point{
        .color = color, .height = 0.4f + Smooth(0.42f, 0.45f, onPaper) * 0.6f + fibers * 0.15f, .roughness = 0.88f};
    constexpr float Holes[][2] = {{0.58f, 0.44f}, {0.41f, 0.6f}, {0.66f, 0.68f}, {0.3f, 0.36f}, {0.53f, 0.27f}};
    for (const auto& hole : Holes) {
        const float distance = Distance(u, v, hole[0], hole[1]);
        if (distance < 0.012f) {
            point.color =
                distance < 0.006f ? Rgb{0.06f, 0.05f, 0.04f} : Mix(point.color, Rgb{0.35f, 0.32f, 0.28f}, 0.5f);
            point.height -= distance < 0.006f ? 2.0f : 0.3f;
            point.occlusion = distance < 0.006f ? 0.3f : 0.8f;
        }
    }
    for (const float x : {0.07f, 0.93f}) {
        for (const float y : {0.07f, 0.93f}) {
            if (std::abs(u - x) < 0.012f && std::abs(v - y) < 0.002f) {
                point.color = Rgb{0.6f, 0.6f, 0.62f};
                point.height += 0.6f;
                point.roughness = 0.3f;
                point.metallic = 1.0f;
            }
        }
    }
    return point;
}

} // namespace

SurfaceTextures Crate(std::uint32_t size) {
    return PaintProp(size, CratePoint,
                     {{.radius = 3.0f, .depth = 2.0f, .dirt = 0.4f, .occlusion = 0.5f, .wear = 0.08f}});
}

SurfaceTextures Barrel(std::uint32_t size) {
    return PaintProp(size, [](float u, float v, float) { return DrumPoint(u, v, {{0.16f, 0.3f, 0.5f}, 1400, false}); },
                     {{.radius = 2.0f, .depth = 0.8f, .dirt = 0.35f, .occlusion = 0.3f}});
}

SurfaceTextures ExplosiveBarrel(std::uint32_t size) {
    return PaintProp(size, [](float u, float v, float) { return DrumPoint(u, v, {{0.66f, 0.08f, 0.06f}, 1450, true}); },
                     {{.radius = 2.0f, .depth = 0.8f, .dirt = 0.35f, .occlusion = 0.3f}});
}

SurfaceTextures Plank(std::uint32_t size) {
    return PaintProp(size, PlankPoint, {{.radius = 2.0f, .depth = 1.0f, .dirt = 0.25f, .occlusion = 0.3f}});
}

SurfaceTextures Beam(std::uint32_t size) {
    return PaintProp(size, BeamPoint, {{.radius = 2.0f, .depth = 1.0f, .dirt = 0.35f, .occlusion = 0.4f}});
}

SurfaceTextures Ball(std::uint32_t size) {
    return PaintProp(size, BallPoint, {{.radius = 1.5f, .depth = 0.8f, .dirt = 0.3f, .occlusion = 0.3f}});
}

SurfaceTextures Cone(std::uint32_t size) {
    return PaintProp(size, ConePoint, {{.radius = 1.5f, .depth = 0.5f, .dirt = 0.3f, .occlusion = 0.2f}});
}

SurfaceTextures Wheel(std::uint32_t size) {
    return PaintProp(size, WheelPoint, {{.radius = 2.0f, .depth = 1.5f, .dirt = 0.35f, .occlusion = 0.4f}});
}

SurfaceTextures TargetDummy(std::uint32_t size) {
    return PaintProp(size, TargetPoint, {{.radius = 2.0f, .depth = 1.0f, .dirt = 0.3f, .occlusion = 0.3f}});
}

} // namespace Generator::Textures
