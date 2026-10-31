#include "foliage_textures.h"

#include "texture_painting.h"

#include <spot.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Generator::Textures {

namespace {

using Spot::Vector2;

// A surface painted stroke over stroke: blades of grass, twigs, leaves and needles, each over the ones before it.
class Canvas {
public:
    Canvas(std::uint32_t size, const SurfacePoint& background, bool tiles)
        : m_size(static_cast<int>(size)), m_tiles(tiles), m_points(static_cast<std::size_t>(size) * size, background),
          m_strokes(static_cast<std::size_t>(size) * size, -1), m_coverage(static_cast<std::size_t>(size) * size) {}

    int GetSize() const { return m_size; }

    void Set(int x, int y, const SurfacePoint& point) { m_points[Index(x, y)] = point; }

    // Starts a stroke, whose pixels are each blended once, by the most it covers them.
    void BeginStroke() { ++m_stroke; }

    // Blends the pixel toward the point by how much the stroke covers it, unless the stroke already covered it as
    // much; outside a canvas that does not tile, nothing happens.
    template <typename Point>
    void Paint(int x, int y, float coverage, Point&& point) {
        if (!m_tiles && (x < 0 || y < 0 || x >= m_size || y >= m_size)) {
            return;
        }
        const std::size_t i = Index(x, y);
        float amount = coverage;
        if (m_strokes[i] == m_stroke) {
            if (coverage <= m_coverage[i]) {
                return;
            }
            amount = (coverage - m_coverage[i]) / (1.0f - m_coverage[i]);
        }
        m_strokes[i] = m_stroke;
        m_coverage[i] = coverage;
        const SurfacePoint sample = point();
        if (m_shadow) {
            if (sample.height > m_points[i].height) {
                m_points[i].color = m_points[i].color * (1.0f - amount * 0.14f);
                m_points[i].occlusion *= 1.0f - amount * 0.13f;
            }
        } else {
            Blend(m_points[i], sample, amount);
        }
    }

    void SetShadow(bool shadow) { m_shadow = shadow; }

    SurfaceTextures Encode() const { return EncodeSurface(static_cast<std::uint32_t>(m_size), m_points); }

private:
    std::size_t Index(int x, int y) const {
        return static_cast<std::size_t>(Wrap(y, m_size)) * static_cast<std::size_t>(m_size) +
               static_cast<std::size_t>(Wrap(x, m_size));
    }

    static void Blend(SurfacePoint& to, const SurfacePoint& from, float amount) {
        to.color = Mix(to.color, from.color, amount);
        to.height += (from.height - to.height) * amount;
        to.occlusion += (from.occlusion - to.occlusion) * amount;
        to.roughness += (from.roughness - to.roughness) * amount;
        to.metallic += (from.metallic - to.metallic) * amount;
        to.alpha += (1.0f - to.alpha) * amount;
    }

    int m_size;
    bool m_tiles;
    bool m_shadow = false;
    std::vector<SurfacePoint> m_points;
    // Which stroke last painted each pixel, and how much it covered it.
    std::vector<int> m_strokes;
    std::vector<float> m_coverage;
    int m_stroke = 0;
};

// A stroke from its root along a direction of unit length, in pixels, curving toward its side (the direction turned
// a quarter clockwise on screen) by the bend times its length at its tip.
struct Stroke {
    Vector2 root;
    Vector2 direction;
    float length = 0.0f;
    float bend = 0.0f;
};

Vector2 Normalized(Vector2 v) {
    const float length = std::sqrt(v.x * v.x + v.y * v.y);
    return length > 1e-6f ? v / length : Vector2{0.0f, -1.0f};
}

// A direction picked from two numbers from 0 to 1, without trigonometry, whose results may differ between machines.
Vector2 RandomDirection(float a, float b) {
    return Normalized({a * 2.0f - 1.0f, b * 2.0f - 1.0f});
}

// The direction turned toward its side by the turn: 1 for 45 degrees.
Vector2 Turned(Vector2 direction, float turn) {
    return Normalized(direction + Vector2{-direction.y, direction.x} * turn);
}

Vector2 PointAlong(const Stroke& stroke, float s) {
    const Vector2 side{-stroke.direction.y, stroke.direction.x};
    return stroke.root + stroke.direction * (stroke.length * s) + side * (stroke.bend * stroke.length * s * s);
}

Vector2 DirectionAlong(const Stroke& stroke, float s) {
    const Vector2 side{-stroke.direction.y, stroke.direction.x};
    return Normalized(stroke.direction + side * (stroke.bend * 2.0f * s));
}

// Paints the stroke in straight segments: width gives how wide it is in pixels along it (from 0 at its root to 1 at
// its tip), and paint what it is at a point of it, from how far along it is, how far across (-1 to 1) and its half
// width in pixels there. Each pixel blends toward it by how much of it the stroke covers, edges smoothed over a pixel.
template <typename Width, typename Paint>
void Draw(Canvas& canvas, const Stroke& stroke, Width&& width, Paint&& paint) {
    canvas.BeginStroke();
    const int segments = std::clamp(static_cast<int>(stroke.length / 6.0f) + 1, 1, 12);
    for (int segment = 0; segment < segments; ++segment) {
        const float s0 = static_cast<float>(segment) / static_cast<float>(segments);
        const float s1 = static_cast<float>(segment + 1) / static_cast<float>(segments);
        const Vector2 a = PointAlong(stroke, s0);
        const Vector2 ab = PointAlong(stroke, s1) - a;
        const float abLengthSquared = std::max(Dot(ab, ab), 1e-6f);
        const float abLength = std::sqrt(abLengthSquared);
        const float reach = std::max(width(s0), width(s1)) * 0.5f + 1.5f;
        const int left = static_cast<int>(std::floor(std::min(a.x, a.x + ab.x) - reach));
        const int right = static_cast<int>(std::ceil(std::max(a.x, a.x + ab.x) + reach));
        const int top = static_cast<int>(std::floor(std::min(a.y, a.y + ab.y) - reach));
        const int bottom = static_cast<int>(std::ceil(std::max(a.y, a.y + ab.y) + reach));
        // Pixels farther than the widest part of the segment are passed over before anything else is worked out,
        // as most of the box around a thin, slanting segment is.
        const float farthest = (reach - 1.0f) * (reach - 1.0f);
        for (int y = top; y <= bottom; ++y) {
            const float offsetY = static_cast<float>(y) + 0.5f - a.y;
            for (int x = left; x <= right; ++x) {
                const float offsetX = static_cast<float>(x) + 0.5f - a.x;
                float along = (offsetX * ab.x + offsetY * ab.y) / abLengthSquared;
                along = along < 0.0f ? 0.0f : (along > 1.0f ? 1.0f : along);
                const float apartX = offsetX - ab.x * along;
                const float apartY = offsetY - ab.y * along;
                const float distanceSquared = apartX * apartX + apartY * apartY;
                if (distanceSquared > farthest) {
                    continue;
                }
                const float distance = std::sqrt(distanceSquared);
                const float s = s0 + (s1 - s0) * along;
                const float halfWidth = width(s) * 0.5f;
                const float coverage = std::clamp(halfWidth + 0.5f - distance, 0.0f, std::min(halfWidth * 2.0f, 1.0f));
                if (coverage <= 0.0f) {
                    continue;
                }
                canvas.Paint(x, y, coverage, [&] {
                    const float across = std::clamp(
                        (ab.x * apartY - ab.y * apartX) / (abLength * std::max(halfWidth, 0.25f)), -1.0f, 1.0f);
                    return paint(s, across, halfWidth);
                });
            }
        }
    }
}

float RandomOf(int i, std::uint32_t seed) {
    return Random(i, 0, seed);
}

// A blade narrows to a point over its last third.
auto BladeWidth(float width) {
    return [width](float s) { return width * std::min(1.0f, (1.0f - s) * 3.0f) * (1.0f - s * 0.25f); };
}

} // namespace

SurfaceTextures PaintGrass(std::uint32_t size) {
    // Lengths are in the pixels of a texture 1024 wide.
    const float k = static_cast<float>(size) / 1024.0f;
    const float extent = static_cast<float>(size);
    // How high the tallest blades stand above the soil, in pixels, for the normals.
    constexpr float Canopy = 2.8f;
    // Periodic fields share the same coordinates across every layer, including strokes crossing the tile edge.
    const auto drynessAt = [extent](Vector2 point) {
        const float u = point.x / extent;
        const float v = point.y / extent;
        const float warpX = (Fractal(u, v, 13, 17, 3, 1130) - 0.5f) * 0.12f;
        const float warpY = (Fractal(u, v, 17, 11, 3, 1131) - 0.5f) * 0.12f;
        const float patches = Smooth(0.28f, 0.76f, Fractal(u + warpX, v + warpY, 5, 7, 4, 1044));
        return patches * 0.65f + Fractal(u, v, 37, 29, 3, 1046) * 0.35f;
    };
    const auto densityAt = [extent](Vector2 point) {
        const float u = point.x / extent;
        const float v = point.y / extent;
        const float gaps = Smooth(0.30f, 0.52f, Fractal(u, v, 19, 23, 4, 1091));
        const float clumps = Fractal(u, v, 23, 19, 3, 1092);
        return (0.48f + gaps * 0.52f) * (0.7f + clumps * 0.3f);
    };
    Canvas canvas(size, {}, true);
    for (int y = 0; y < canvas.GetSize(); ++y) {
        for (int x = 0; x < canvas.GetSize(); ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / extent;
            const float v = (static_cast<float>(y) + 0.5f) / extent;
            // Fine grains and larger compacted patches remain visible between the blades.
            const float soil = Fractal(u, v, 31, 27, 4, 1001);
            const float grain = Random(x, y, 1002);
            canvas.Set(x, y,
                       {.color = Mix(Rgb{0.12f, 0.15f, 0.07f}, Rgb{0.27f, 0.25f, 0.13f}, soil * 0.75f + grain * 0.25f),
                        .height = soil * 0.35f + grain * 0.12f,
                        .occlusion = 0.53f,
                        .roughness = 0.95f});
        }
    }

    // The mat of dead straw, flat on the soil.
    const int straws = 26000;
    for (int i = 0; i < straws; ++i) {
        const Vector2 root{RandomOf(i, 1011) * extent, RandomOf(i, 1012) * extent};
        if (RandomOf(i, 1021) > 0.15f + densityAt(root) * 0.6f + drynessAt(root) * 0.25f) {
            continue;
        }
        const Rgb straw = Mix(Rgb{0.36f, 0.29f, 0.18f}, Rgb{0.56f, 0.48f, 0.31f}, RandomOf(i, 1017)) *
                          (0.8f + RandomOf(i, 1018) * 0.2f);
        const float lift = RandomOf(i, 1019) * 0.25f;
        Draw(canvas,
             {.root = root,
              .direction = RandomDirection(RandomOf(i, 1013), RandomOf(i, 1014)),
              .length = (8.0f + RandomOf(i, 1015) * 28.0f) * k,
              .bend = (RandomOf(i, 1016) - 0.5f) * 0.5f},
             BladeWidth((0.8f + RandomOf(i, 1020) * 1.6f) * k), [&](float, float across, float) {
                 return SurfacePoint{.color = straw * (0.9f + (1.0f - across * across) * 0.15f),
                                     .height = 0.35f + lift + (1.0f - across * across) * 0.12f,
                                     .occlusion = 0.6f + lift * 0.2f,
                                     .roughness = 0.9f};
             });
    }

    // The living blades, gathered so the lowest are painted first.
    struct Blade {
        Stroke stroke;
        float width;
        // From 0 at the soil to 1 at the top of the grass, at the blade's root and its tip.
        float rootHeight;
        float tipHeight;
        Rgb color;
        float roughness;
        int seed;
    };
    std::vector<Blade> blades;
    const auto bladeColor = [](int i, float dryness) {
        Rgb color = Mix(Rgb{0.19f, 0.32f, 0.09f}, Rgb{0.43f, 0.55f, 0.20f}, RandomOf(i, 1031));
        color = Mix(color, Rgb{0.4f, 0.43f, 0.13f}, RandomOf(i, 1032) * RandomOf(i, 1032) * 0.45f + dryness * 0.65f);
        if (RandomOf(i, 1033) < 0.06f + dryness * 0.32f) {
            color = Mix(Rgb{0.5f, 0.44f, 0.25f}, Rgb{0.64f, 0.57f, 0.35f}, RandomOf(i, 1034));
        }
        return color;
    };
    // Tufts' blades lean out from their center: those standing up are the tallest, and the shortest seen from above.
    const int tufts = 10000;
    for (int tuft = 0; tuft < tufts; ++tuft) {
        const Vector2 center{RandomOf(tuft, 1041) * extent, RandomOf(tuft, 1042) * extent};
        if (RandomOf(tuft, 1093) > densityAt(center)) {
            continue;
        }
        const float habitat = Fractal(center.x / extent, center.y / extent, 17, 21, 3, 1120);
        const float stature = 0.35f + habitat * 0.65f + RandomOf(tuft, 1043) * 0.35f;
        // Dryness varies independently of coverage, so thin patches need not all be brown.
        const float dryness = drynessAt(center);
        const Vector2 tuftDirection = RandomDirection(RandomOf(tuft, 1095), RandomOf(tuft, 1096));
        const float tuftWidth = 0.8f + RandomOf(tuft, 1097) * 1.1f;
        const int count = 5 + static_cast<int>((RandomOf(tuft, 1045) + habitat) * 6.0f);
        for (int b = 0; b < count; ++b) {
            const int i = tuft * 32 + b;
            const Vector2 scattered = RandomDirection(RandomOf(i, 1046), RandomOf(i, 1047));
            const Vector2 direction = Normalized(tuftDirection * 0.65f + scattered);
            const float lean = 0.25f + RandomOf(i, 1048) * 0.75f;
            const float fullLength = (24.0f + RandomOf(i, 1049) * RandomOf(i, 1049) * 105.0f) * k * stature;
            blades.push_back({.stroke = {.root = center + direction * (RandomOf(i, 1050) * 3.0f * k),
                                         .direction = direction,
                                         .length = fullLength * lean,
                                         .bend = (RandomOf(i, 1051) - 0.5f) * 1.3f},
                              .width = (0.9f + RandomOf(i, 1052) * RandomOf(i, 1052) * 2.2f) * tuftWidth * k,
                              .rootHeight = 0.08f + stature * 0.08f,
                              .tipHeight = stature * (1.0f - lean * 0.45f),
                              .color = bladeColor(i, dryness),
                              .roughness = 0.70f + RandomOf(i, 1053) * 0.22f,
                              .seed = i});
        }
    }
    const int loose = 10000;
    for (int b = 0; b < loose; ++b) {
        const int i = (1 << 24) | b;
        const Vector2 root{RandomOf(i, 1054) * extent, RandomOf(i, 1055) * extent};
        if (RandomOf(i, 1094) > densityAt(root)) {
            continue;
        }
        const float lean = 0.4f + RandomOf(i, 1048) * 0.6f;
        blades.push_back({.stroke = {.root = root,
                                     .direction = RandomDirection(RandomOf(i, 1046), RandomOf(i, 1047)),
                                     .length = (24.0f + RandomOf(i, 1049) * 40.0f) * k * lean,
                                     .bend = (RandomOf(i, 1051) - 0.5f) * 1.3f},
                          .width = (0.55f + RandomOf(i, 1052) * RandomOf(i, 1052) * 2.1f) * k,
                          .rootHeight = 0.1f,
                          .tipHeight = 0.3f + RandomOf(i, 1056) * 0.6f * (1.0f - lean * 0.4f),
                          .color = bladeColor(i, drynessAt(root)),
                          .roughness = 0.70f + RandomOf(i, 1053) * 0.22f,
                          .seed = i});
    }
    std::stable_sort(blades.begin(), blades.end(),
                     [](const Blade& a, const Blade& b) { return a.tipHeight < b.tipHeight; });
    for (const Blade& blade : blades) {
        const float fold = RandomOf(blade.seed, 1100);
        const auto width = [&](float s) {
            const float tip = RandomOf(blade.seed, 1121) < 0.18f ? 0.22f : 0.0f;
            const float taper = std::sqrt(std::max(tip, 1.0f - s));
            const float crease = RandomOf(blade.seed, 1132) < 0.25f ? 0.65f : 0.3f;
            const float twist = 1.0f - crease + crease * std::min(1.0f, std::abs(s - fold) * 3.0f);
            return blade.width * taper * twist * (0.65f + 0.35f * Smooth(0.0f, 0.18f, s));
        };
        Stroke shadow = blade.stroke;
        shadow.root = shadow.root + Vector2{0.65f, 0.85f} * k;
        canvas.SetShadow(true);
        Draw(
            canvas, shadow, [&](float s) { return width(s) + 0.7f * k; },
            [&](float s, float, float) {
                return SurfacePoint{.height = (blade.rootHeight + (blade.tipHeight - blade.rootHeight) * s) * Canopy};
            });
        canvas.SetShadow(false);
        Draw(canvas, blade.stroke, width, [&](float s, float across, float) {
            const float height = blade.rootHeight + (blade.tipHeight - blade.rootHeight) * s;
            // Subtle folds and root shading avoid a raised, outlined leaf appearance.
            const float round = 1.0f - across * across;
            const float midrib = Smooth(0.25f, 0.0f, std::abs(across)) * (1.0f - s * 0.5f);
            const float fleck =
                ValueNoise(s * 19.0f, across * 2.0f, 19, 4, 1110u + static_cast<std::uint32_t>(blade.seed));
            const float bleaching = Smooth(0.55f + fold * 0.3f, 1.0f, s) * RandomOf(blade.seed, 1101) * 0.45f;
            const Rgb color = Mix(blade.color, Rgb{0.61f, 0.57f, 0.32f}, bleaching);
            return SurfacePoint{.color =
                                    color * (0.97f + fleck * 0.06f) *
                                    ((0.80f + std::min(height, 1.0f) * 0.25f) *
                                     (0.94f + round * 0.06f + across * 0.025f + s * 0.04f) * (1.0f + midrib * 0.035f)),
                                .height = height * Canopy + round * 0.12f + midrib * 0.035f,
                                .occlusion = 0.66f + std::min(height, 1.0f) * 0.34f,
                                .roughness = blade.roughness};
        });
    }

    return canvas.Encode();
}

SurfaceTextures PaintLeaves(std::uint32_t size) {
    const float k = static_cast<float>(size) / 512.0f;
    const float extent = static_cast<float>(size);
    Canvas canvas(size, {.color = Rgb{0.2f, 0.3f, 0.08f}, .occlusion = 1.0f, .roughness = 0.6f, .alpha = 0.0f}, false);
    const auto width = [k](float s) {
        if (s < 0.1f) {
            return 2.0f * k;
        }
        const float b = (s - 0.1f) / 0.9f;
        const float outline = std::pow(std::max(std::sin(b * Spot::Pi), 0.0f), 0.65f);
        const float lobes = 0.77f + 0.23f * std::cos(b * Spot::Pi * 10.0f + 0.4f);
        return std::max(1.0f * k, 216.0f * k * outline * lobes);
    };
    const auto paint = [k](float s, float across, float halfWidth) {
        if (s < 0.1f) {
            return SurfacePoint{.color = {0.31f, 0.29f, 0.12f}, .height = k, .roughness = 0.72f};
        }
        const float b = (s - 0.1f) / 0.9f;
        const float midrib = Smooth(2.0f * k, 0.4f * k, std::abs(across) * halfWidth);
        const float phase = b * 6.0f - std::abs(across) * 0.85f;
        const float vein = Smooth(0.04f, 0.005f, std::abs(phase - std::floor(phase + 0.5f)));
        const float finePhase = b * 38.0f + std::abs(across) * 5.0f;
        const float fineVein = Smooth(0.05f, 0.0f, std::abs(finePhase - std::floor(finePhase + 0.5f)));
        const float mottling = ValueNoise(b * 24.0f, across * 9.0f + 9.0f, 24, 18, 1921);
        const float cells = ValueNoise(b * 180.0f, across * 75.0f + 75.0f, 180, 150, 1922);
        const float edge = Smooth(0.87f, 1.0f, std::abs(across));
        Rgb color = Mix(Rgb{0.15f, 0.255f, 0.065f}, Rgb{0.29f, 0.38f, 0.12f}, mottling);
        color = Mix(color, Rgb{0.36f, 0.34f, 0.13f}, edge * 0.22f);
        color = Mix(color, Rgb{0.41f, 0.46f, 0.22f}, midrib * 0.45f + vein * 0.09f);
        return SurfacePoint{.color = color * (0.96f + cells * 0.08f),
                            .height = k * (1.4f * (1.0f - across * across) + midrib * 0.4f + vein * 0.16f +
                                           fineVein * 0.04f + cells * 0.06f),
                            .occlusion = 1.0f,
                            .roughness = 0.48f + edge * 0.14f + mottling * 0.09f + fineVein * 0.025f};
    }; // Evaluate the broad blade directly: overlapping stroke capsules would leave bands in its veins.
    for (int y = 0; y < canvas.GetSize(); ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / extent;
        const float s = (0.98f - v) / 0.96f;
        if (s < 0.0f || s > 1.0f) {
            continue;
        }
        const float halfWidth = width(s);
        const float center = extent * (0.5f + 0.018f * s * s);
        for (int x = 0; x < canvas.GetSize(); ++x) {
            const float across = (static_cast<float>(x) + 0.5f - center) / halfWidth;
            const float coverage =
                std::clamp(halfWidth + 0.5f - std::abs(static_cast<float>(x) + 0.5f - center), 0.0f, 1.0f);
            if (coverage > 0.0f) {
                auto point = paint(s, across, halfWidth);
                point.alpha = coverage;
                canvas.Set(x, y, point);
            }
        }
    }
    return canvas.Encode();
}

SurfaceTextures PaintNeedles(std::uint32_t size) {
    const float k = static_cast<float>(size) / 512.0f;
    const float extent = static_cast<float>(size);
    Canvas canvas(size, {.color = Rgb{0.1f, 0.19f, 0.09f}, .occlusion = 1.0f, .roughness = 0.6f, .alpha = 0.0f}, false);
    const auto paintTwig = [](float, float across, float) {
        return SurfacePoint{.color = Rgb{0.33f, 0.24f, 0.15f} * (0.75f + (1.0f - across * across) * 0.3f),
                            .height = 1.0f + (1.0f - across * across) * 1.5f,
                            .occlusion = 0.6f,
                            .roughness = 0.85f};
    };
    // The twig and its shoots, shorter toward its tip, as a spray narrows.
    std::vector<Stroke> twigs;
    twigs.push_back(
        {.root = {extent * 0.5f, extent * 0.99f}, .direction = {0.0f, -1.0f}, .length = extent * 0.93f, .bend = 0.03f});
    for (int i = 0; i < 13; ++i) {
        const float t = 0.08f + 0.062f * static_cast<float>(i);
        const float side = i % 2 == 0 ? 1.0f : -1.0f;
        twigs.push_back({.root = PointAlong(twigs[0], t),
                         .direction = Turned(DirectionAlong(twigs[0], t), side * (1.1f + RandomOf(i, 2201) * 0.3f)),
                         .length = extent * (0.44f - t * 0.38f) * (0.9f + RandomOf(i, 2202) * 0.2f),
                         .bend = -side * 0.12f});
    }
    for (std::size_t i = 0; i < twigs.size(); ++i) {
        const float base = i == 0 ? 5.0f : 3.0f;
        Draw(canvas, twigs[i], [&](float s) { return (base - s * base * 0.5f) * k; }, paintTwig);
    }
    // Needles on both sides of every twig, leaning toward its tip; their ends are new growth.
    int index = 0;
    for (const Stroke& twig : twigs) {
        const int count = static_cast<int>(twig.length / (2.3f * k));
        for (int i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(count);
            const Vector2 at = PointAlong(twig, t);
            const Vector2 tangent = DirectionAlong(twig, t);
            for (const float side : {-1.0f, 1.0f}) {
                ++index;
                Rgb color = Mix(Rgb{0.08f, 0.18f, 0.12f}, Rgb{0.2f, 0.32f, 0.15f}, RandomOf(index, 2211));
                color = Mix(color, Rgb{0.36f, 0.46f, 0.2f}, Smooth(0.65f, 1.0f, t) * 0.65f);
                const float lift = RandomOf(index, 2213);
                Draw(
                    canvas,
                    {.root = at,
                     .direction = Turned(tangent, side * (1.3f + (RandomOf(index, 2214) - 0.5f) * 0.8f)),
                     .length = (18.0f + RandomOf(index, 2212) * 10.0f) * k * (1.0f - t * 0.35f),
                     .bend = (RandomOf(index, 2215) - 0.5f) * 0.3f},
                    [k](float s) { return 2.8f * k * (1.0f - s * 0.75f); },
                    [&](float s, float across, float) {
                        const float round = 1.0f - across * across;
                        const float ridge = Smooth(0.3f, 0.0f, std::abs(across));
                        return SurfacePoint{.color = color *
                                                     (0.75f + lift * 0.25f + round * 0.12f + s * 0.12f + ridge * 0.06f),
                                            .height = 2.0f + lift * 1.5f + round * 0.8f,
                                            .occlusion = 0.6f + lift * 0.4f,
                                            .roughness = 0.4f + (1.0f - round) * 0.16f + lift * 0.08f};
                    });
            }
        }
    }
    return canvas.Encode();
}

} // namespace Generator::Textures
