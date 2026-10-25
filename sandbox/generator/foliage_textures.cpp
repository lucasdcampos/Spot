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
        Blend(m_points[i], point(), amount);
    }

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
    constexpr float Canopy = 7.0f;
    Canvas canvas(size, {}, true);
    for (int y = 0; y < canvas.GetSize(); ++y) {
        for (int x = 0; x < canvas.GetSize(); ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / extent;
            const float v = (static_cast<float>(y) + 0.5f) / extent;
            // Barely seen through the grass, so a single octave does.
            const float soil = ValueNoise(u * 24.0f, v * 24.0f, 24, 24, 1001);
            canvas.Set(x, y,
                       {.color = Mix(Rgb{0.16f, 0.12f, 0.08f}, Rgb{0.27f, 0.21f, 0.14f}, soil),
                        .height = soil * 0.5f,
                        .occlusion = 0.3f,
                        .roughness = 0.95f});
        }
    }

    // The mat of dead straw, flat on the soil.
    const int straws = static_cast<int>(9000.0f * k * k);
    for (int i = 0; i < straws; ++i) {
        const Rgb straw = Mix(Rgb{0.36f, 0.29f, 0.18f}, Rgb{0.56f, 0.48f, 0.31f}, RandomOf(i, 1017)) *
                          (0.55f + RandomOf(i, 1018) * 0.3f);
        const float lift = RandomOf(i, 1019) * 0.8f;
        Draw(canvas,
             {.root = {RandomOf(i, 1011) * extent, RandomOf(i, 1012) * extent},
              .direction = RandomDirection(RandomOf(i, 1013), RandomOf(i, 1014)),
              .length = (12.0f + RandomOf(i, 1015) * 22.0f) * k,
              .bend = (RandomOf(i, 1016) - 0.5f) * 0.5f},
             BladeWidth((1.3f + RandomOf(i, 1020) * 0.9f) * k), [&](float, float across, float) {
                 return SurfacePoint{.color = straw * (0.9f + (1.0f - across * across) * 0.15f),
                                     .height = 0.6f + lift + (1.0f - across * across) * 0.4f,
                                     .occlusion = 0.4f + lift * 0.1f,
                                     .roughness = 0.9f};
             });
    }

    // Broadleaf weeds lying flat among the roots, as plantain and dandelion grow: rosettes of long, glossy leaves
    // with a pale vein down each.
    const int rosettes = static_cast<int>(36.0f * k * k);
    for (int r = 0; r < rosettes; ++r) {
        const Vector2 center{RandomOf(r, 1071) * extent, RandomOf(r, 1072) * extent};
        const int leaves = 5 + static_cast<int>(RandomOf(r, 1073) * 4.0f);
        const Rgb color = Mix(Rgb{0.13f, 0.24f, 0.07f}, Rgb{0.2f, 0.32f, 0.09f}, RandomOf(r, 1074));
        Vector2 direction = RandomDirection(RandomOf(r, 1075), RandomOf(r, 1076));
        for (int leaf = 0; leaf < leaves; ++leaf) {
            const int i = r * 16 + leaf;
            const float length = (22.0f + RandomOf(i, 1077) * 18.0f) * k;
            const float width = length * (0.3f + RandomOf(i, 1078) * 0.12f);
            // Spread evenly around: Turned() takes the tangent of the angle between leaves, a turn over their count.
            constexpr float Spread[] = {3.0777f, 1.7321f, 1.254f, 1.0f};
            direction = Turned(direction, Spread[leaves - 5]);
            Draw(
                canvas,
                {.root = center, .direction = direction, .length = length, .bend = (RandomOf(i, 1079) - 0.5f) * 0.3f},
                [&](float s) { return width * std::sqrt(std::max(s * (1.0f - s) * 4.0f, 0.0f)) * (1.1f - s * 0.4f); },
                [&](float s, float across, float) {
                    const float vein = Smooth(0.18f, 0.0f, std::abs(across)) * (1.0f - s);
                    return SurfacePoint{.color = Mix(color * (0.85f + s * 0.25f), Rgb{0.42f, 0.5f, 0.3f}, vein * 0.5f),
                                        .height = 1.2f + s * 0.8f + (1.0f - across * across) * 0.5f - vein * 0.3f,
                                        .occlusion = 0.45f + s * 0.15f,
                                        .roughness = 0.5f};
                });
        }
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
    };
    std::vector<Blade> blades;
    const auto bladeColor = [](int i, float dryness) {
        Rgb color = Mix(Rgb{0.15f, 0.26f, 0.06f}, Rgb{0.32f, 0.46f, 0.12f}, RandomOf(i, 1031));
        color = Mix(color, Rgb{0.4f, 0.43f, 0.13f}, RandomOf(i, 1032) * RandomOf(i, 1032) * 0.45f + dryness * 0.3f);
        if (RandomOf(i, 1033) < 0.035f + dryness * 0.05f) {
            color = Mix(Rgb{0.5f, 0.44f, 0.25f}, Rgb{0.64f, 0.57f, 0.35f}, RandomOf(i, 1034));
        }
        return color;
    };
    // Tufts' blades lean out from their center: those standing up are the tallest, and the shortest seen from above.
    const int tufts = static_cast<int>(5200.0f * k * k);
    for (int tuft = 0; tuft < tufts; ++tuft) {
        const Vector2 center{RandomOf(tuft, 1041) * extent, RandomOf(tuft, 1042) * extent};
        const float stature = 0.55f + RandomOf(tuft, 1043) * 0.45f;
        // Broad patches of drier grass, faint so their repeats do not show from afar.
        const float dryness = Smooth(0.5f, 0.8f, Fractal(center.x / extent, center.y / extent, 3, 3, 3, 1044));
        const int count = 8 + static_cast<int>(RandomOf(tuft, 1045) * 9.0f);
        for (int b = 0; b < count; ++b) {
            const int i = tuft * 32 + b;
            const Vector2 direction = RandomDirection(RandomOf(i, 1046), RandomOf(i, 1047));
            const float lean = 0.25f + RandomOf(i, 1048) * 0.75f;
            const float fullLength = (18.0f + RandomOf(i, 1049) * 20.0f) * k * stature;
            blades.push_back({.stroke = {.root = center + direction * (RandomOf(i, 1050) * 3.0f * k),
                                         .direction = direction,
                                         .length = fullLength * lean,
                                         .bend = (RandomOf(i, 1051) - 0.5f) * 0.6f},
                              .width = (2.0f + RandomOf(i, 1052) * 1.1f) * k,
                              .rootHeight = 0.15f + stature * 0.15f,
                              .tipHeight = stature * (1.0f - lean * 0.45f),
                              .color = bladeColor(i, dryness),
                              .roughness = 0.74f + RandomOf(i, 1053) * 0.14f});
        }
    }
    const int loose = static_cast<int>(20000.0f * k * k);
    for (int b = 0; b < loose; ++b) {
        const int i = (1 << 24) | b;
        const float lean = 0.4f + RandomOf(i, 1048) * 0.6f;
        blades.push_back({.stroke = {.root = {RandomOf(i, 1054) * extent, RandomOf(i, 1055) * extent},
                                     .direction = RandomDirection(RandomOf(i, 1046), RandomOf(i, 1047)),
                                     .length = (14.0f + RandomOf(i, 1049) * 22.0f) * k * lean,
                                     .bend = (RandomOf(i, 1051) - 0.5f) * 0.6f},
                          .width = (1.8f + RandomOf(i, 1052) * 1.0f) * k,
                          .rootHeight = 0.1f,
                          .tipHeight = 0.3f + RandomOf(i, 1056) * 0.6f * (1.0f - lean * 0.4f),
                          .color = bladeColor(i, 0.0f),
                          .roughness = 0.74f + RandomOf(i, 1053) * 0.14f});
    }
    std::stable_sort(blades.begin(), blades.end(),
                     [](const Blade& a, const Blade& b) { return a.tipHeight < b.tipHeight; });
    for (const Blade& blade : blades) {
        Draw(canvas, blade.stroke, BladeWidth(blade.width), [&](float s, float across, float) {
            const float height = blade.rootHeight + (blade.tipHeight - blade.rootHeight) * s;
            // Rounded across and lighter toward the tip, with a paler midrib down it; lower down, in the shade of the
            // others.
            const float round = 1.0f - across * across;
            const float midrib = Smooth(0.25f, 0.0f, std::abs(across)) * (1.0f - s * 0.5f);
            return SurfacePoint{.color = blade.color * ((0.7f + height * 0.5f) * (0.86f + round * 0.14f + s * 0.1f) *
                                                        (1.0f + midrib * 0.12f)),
                                .height = height * Canopy + round * 0.6f + midrib * 0.2f,
                                .occlusion = 0.35f + height * 0.65f,
                                .roughness = blade.roughness};
        });
    }

    // A little clover among the grass: three round leaflets, each with a pale band.
    const int clovers = static_cast<int>(150.0f * k * k);
    for (int c = 0; c < clovers; ++c) {
        const Vector2 center{RandomOf(c, 1061) * extent, RandomOf(c, 1062) * extent};
        const Vector2 first = RandomDirection(RandomOf(c, 1063), RandomOf(c, 1064));
        const float height = 0.5f + RandomOf(c, 1065) * 0.5f;
        const float leaflet = (5.0f + RandomOf(c, 1066) * 3.0f) * k;
        const Rgb color = Mix(Rgb{0.17f, 0.28f, 0.09f}, Rgb{0.24f, 0.36f, 0.12f}, RandomOf(c, 1067));
        // A third of a turn apart.
        constexpr float Sine = 0.8660254f;
        const Vector2 directions[] = {first,
                                      {-0.5f * first.x - Sine * first.y, Sine * first.x - 0.5f * first.y},
                                      {-0.5f * first.x + Sine * first.y, -Sine * first.x - 0.5f * first.y}};
        for (const Vector2 direction : directions) {
            Draw(
                canvas, {.root = center, .direction = direction, .length = leaflet},
                [&](float s) { return leaflet * 1.7f * std::sqrt(std::max(s * (1.0f - s), 0.0f)); },
                [&](float s, float across, float) {
                    const float band = Smooth(0.15f, 0.0f, std::abs(s - 0.45f)) * 0.25f;
                    return SurfacePoint{.color = Mix(color * (0.7f + height * 0.5f), Rgb{0.5f, 0.6f, 0.4f}, band),
                                        .height = height * Canopy + (1.0f - across * across) * 0.8f,
                                        .occlusion = 0.35f + height * 0.65f,
                                        .roughness = 0.6f};
                });
        }
    }
    // A few small flowers over the blades: daisies, white around a yellow middle, and yellow buttercups.
    const int flowers = static_cast<int>(10.0f * k * k);
    for (int f = 0; f < flowers; ++f) {
        const Vector2 center{RandomOf(f, 1081) * extent, RandomOf(f, 1082) * extent};
        const bool daisy = RandomOf(f, 1083) < 0.6f;
        const Rgb petal = daisy ? Rgb{0.92f, 0.91f, 0.86f} : Rgb{0.95f, 0.78f, 0.12f};
        const int petals = daisy ? 11 : 5;
        const float reach = (daisy ? 4.5f : 3.5f) * k;
        Vector2 direction = RandomDirection(RandomOf(f, 1084), RandomOf(f, 1085));
        for (int p = 0; p < petals; ++p) {
            // The tangents of a fifth and of an eleventh of a turn.
            direction = Turned(direction, daisy ? 0.6427f : 3.0777f);
            Draw(
                canvas, {.root = center, .direction = direction, .length = reach},
                [&](float s) { return (daisy ? 1.6f : 2.6f) * k * std::sqrt(std::max(s * (1.0f - s) * 4.0f, 0.0f)); },
                [&](float s, float, float) {
                    return SurfacePoint{.color = petal * (0.85f + s * 0.15f),
                                        .height = Canopy * 1.05f + s * 0.3f,
                                        .occlusion = 1.0f,
                                        .roughness = 0.6f};
                });
        }
        Draw(
            canvas, {.root = center - Vector2{0.5f, 0.0f} * k, .direction = {1.0f, 0.0f}, .length = 1.0f * k},
            [&](float) { return (daisy ? 2.2f : 1.6f) * k; },
            [&](float, float, float) {
                return SurfacePoint{.color = daisy ? Rgb{0.9f, 0.68f, 0.1f} : Rgb{0.7f, 0.55f, 0.08f},
                                    .height = Canopy * 1.1f,
                                    .occlusion = 1.0f,
                                    .roughness = 0.7f};
            });
    }
    return canvas.Encode();
}

SurfaceTextures PaintLeaves(std::uint32_t size) {
    const float k = static_cast<float>(size) / 512.0f;
    const float extent = static_cast<float>(size);
    Canvas canvas(size, {.color = Rgb{0.2f, 0.3f, 0.08f}, .occlusion = 1.0f, .roughness = 0.6f, .alpha = 0.0f}, false);
    const auto paintTwig = [](float s, float across, float) {
        return SurfacePoint{.color = Rgb{0.3f, 0.23f, 0.16f} * (0.75f + (1.0f - across * across) * 0.3f + s * 0.1f),
                            .height = 1.0f + (1.0f - across * across) * 2.0f,
                            .occlusion = 0.7f,
                            .roughness = 0.85f};
    };
    const Stroke main{.root = {extent * 0.5f, extent * 0.99f},
                      .direction = Normalized({0.04f, -1.0f}),
                      .length = extent * 0.6f,
                      .bend = 0.08f};
    const Stroke shoots[] = {{.root = PointAlong(main, 0.32f),
                              .direction = Turned(DirectionAlong(main, 0.32f), -1.3f),
                              .length = extent * 0.4f,
                              .bend = 0.15f},
                             {.root = PointAlong(main, 0.53f),
                              .direction = Turned(DirectionAlong(main, 0.53f), 1.2f),
                              .length = extent * 0.36f,
                              .bend = -0.15f}};
    Draw(canvas, main, [k](float s) { return (6.0f - s * 3.5f) * k; }, paintTwig);
    for (const Stroke& shoot : shoots) {
        Draw(canvas, shoot, [k](float s) { return (4.0f - s * 2.0f) * k; }, paintTwig);
    }

    // Leaves grow along each twig from a third of the way to its tip, alternating sides, and one at the tip; the
    // farther along, the more they point the twig's way.
    struct Leaf {
        Stroke stroke;
        float width;
    };
    std::vector<Leaf> leaves;
    const Stroke* twigs[] = {&shoots[0], &shoots[1], &main};
    int index = 0;
    for (const Stroke* twig : twigs) {
        const int count = twig == &main ? 9 : 6;
        for (int i = 0; i <= count; ++i, ++index) {
            const float s = i == count ? 1.0f : 0.3f + 0.7f * static_cast<float>(i) / static_cast<float>(count);
            const float facing = i == count ? 0.0f : (i % 2 == 0 ? 1.0f : -1.0f) * (1.6f - s * 0.9f);
            const float length = (112.0f + RandomOf(index, 1902) * 50.0f) * k * (twig == &main ? 1.0f : 0.9f);
            leaves.push_back({.stroke = {.root = PointAlong(*twig, s),
                                         .direction = Turned(DirectionAlong(*twig, s),
                                                             facing + (RandomOf(index, 1903) - 0.5f) * 0.4f),
                                         .length = length,
                                         .bend = (RandomOf(index, 1904) - 0.5f) * 0.25f},
                              .width = length * (0.36f + RandomOf(index, 1905) * 0.1f)});
        }
    }
    for (std::size_t i = 0; i < leaves.size(); ++i) {
        const Leaf& leaf = leaves[i];
        const int n = static_cast<int>(i);
        // Leaves painted first lie deepest, darker in the others' shade.
        const float depth = static_cast<float>(i) / static_cast<float>(leaves.size());
        Rgb color = Mix(Rgb{0.15f, 0.29f, 0.06f}, Rgb{0.29f, 0.44f, 0.1f}, RandomOf(n, 1911));
        color = Mix(color, Rgb{0.44f, 0.47f, 0.14f}, RandomOf(n, 1912) * RandomOf(n, 1912) * 0.5f);
        const float lobes = 4.0f + RandomOf(n, 1913) * 1.5f;
        // A short stalk, then an oak's blade, widest past its middle, with rounded lobes along both edges.
        const auto width = [&](float s) {
            if (s < 0.08f) {
                return 3.0f * k;
            }
            const float b = (s - 0.08f) / 0.92f;
            const float outline = 2.0f * std::sqrt(std::max(b * (1.0f - b), 0.0f)) * (1.1f - b * 0.3f);
            const float lobe = b > 0.12f ? 0.72f + 0.28f * (Wave(b * lobes + 0.25f) * 0.5f + 0.5f) : 1.0f;
            return std::max(leaf.width * outline * lobe, 3.0f * k);
        };
        Draw(canvas, leaf.stroke, width, [&](float s, float across, float halfWidth) {
            if (s < 0.08f) {
                return SurfacePoint{
                    .color = Rgb{0.3f, 0.3f, 0.12f}, .height = 2.0f, .occlusion = 0.7f, .roughness = 0.7f};
            }
            const float b = (s - 0.08f) / 0.92f;
            const float midrib = Smooth(1.6f * k, 0.4f * k, std::abs(across) * halfWidth);
            // Veins run from the midrib toward the tip and the edges.
            const float phase = b * 7.0f - std::abs(across) * 0.9f;
            const float vein = Smooth(0.07f, 0.0f, std::abs(phase - std::floor(phase + 0.5f))) *
                               (1.0f - Smooth(0.7f, 0.95f, std::abs(across)));
            const float round = 1.0f - across * across;
            Rgb shade = color * ((0.62f + depth * 0.38f) * (0.84f + round * 0.18f));
            shade = Mix(shade, Rgb{0.52f, 0.56f, 0.3f}, midrib * 0.55f + vein * 0.12f);
            return SurfacePoint{.color = shade,
                                .height = 2.0f + depth * 2.0f + round * 2.5f - midrib * 0.8f - vein * 0.3f,
                                .occlusion = 0.55f + depth * 0.45f,
                                .roughness = 0.5f + (1.0f - round) * 0.1f + vein * 0.1f};
        });
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
                Rgb color = Mix(Rgb{0.07f, 0.16f, 0.08f}, Rgb{0.15f, 0.27f, 0.12f}, RandomOf(index, 2211));
                color = Mix(color, Rgb{0.3f, 0.45f, 0.17f}, Smooth(0.7f, 1.0f, t) * 0.7f);
                const float lift = RandomOf(index, 2213);
                Draw(
                    canvas,
                    {.root = at,
                     .direction = Turned(tangent, side * (1.3f + (RandomOf(index, 2214) - 0.5f) * 0.8f)),
                     .length = (18.0f + RandomOf(index, 2212) * 10.0f) * k * (1.0f - t * 0.35f),
                     .bend = (RandomOf(index, 2215) - 0.5f) * 0.3f},
                    [k](float s) { return 2.4f * k * (1.0f - s * 0.7f); },
                    [&](float s, float across, float) {
                        const float round = 1.0f - across * across;
                        return SurfacePoint{.color = color * (0.7f + lift * 0.3f + round * 0.15f + s * 0.1f),
                                            .height = 2.0f + lift * 1.5f + round * 0.8f,
                                            .occlusion = 0.6f + lift * 0.4f,
                                            .roughness = 0.55f};
                    });
            }
        }
    }
    return canvas.Encode();
}

} // namespace Generator::Textures
