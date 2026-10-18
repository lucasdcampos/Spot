#include "icons.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace Generator::Icons {

namespace {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

Point operator-(Point a, Point b) {
    return {a.x - b.x, a.y - b.y};
}

float Dot(Point a, Point b) {
    return a.x * b.x + a.y * b.y;
}

float Length(Point a) {
    return std::sqrt(Dot(a, a));
}

// Signed distances to the edges of shapes, in pixels, negative inside; y grows down, as in the image.
float Circle(Point p, Point center, float radius) {
    return Length(p - center) - radius;
}

// A box between two corners, its corners rounded by the radius.
float Box(Point p, Point min, Point max, float radius = 0.0f) {
    const Point center{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    const float qx = std::abs(p.x - center.x) - ((max.x - min.x) * 0.5f - radius);
    const float qy = std::abs(p.y - center.y) - ((max.y - min.y) * 0.5f - radius);
    const float outside = Length({std::max(qx, 0.0f), std::max(qy, 0.0f)});
    return outside + std::min(std::max(qx, qy), 0.0f) - radius;
}

// A stroke between two points, with round ends.
float Capsule(Point p, Point a, Point b, float radius) {
    const Point pa = p - a;
    const Point ba = b - a;
    const float t = std::clamp(Dot(pa, ba) / Dot(ba, ba), 0.0f, 1.0f);
    return Length({pa.x - ba.x * t, pa.y - ba.y * t}) - radius;
}

// Any simple polygon, grown by the radius to round it.
float Polygon(Point p, std::initializer_list<Point> points, float radius = 0.0f) {
    const std::vector<Point> v(points);
    float distance = Dot(p - v[0], p - v[0]);
    float sign = 1.0f;
    for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i, ++i) {
        const Point e = v[j] - v[i];
        const Point w = p - v[i];
        const float t = std::clamp(Dot(w, e) / Dot(e, e), 0.0f, 1.0f);
        const Point b{w.x - e.x * t, w.y - e.y * t};
        distance = std::min(distance, Dot(b, b));
        const bool above = p.y >= v[i].y;
        const bool below = p.y < v[j].y;
        const bool left = e.x * w.y > e.y * w.x;
        if ((above && below && left) || (!above && !below && !left)) {
            sign = -sign;
        }
    }
    return sign * std::sqrt(distance) - radius;
}

float Union(std::initializer_list<float> distances) {
    return std::min(distances);
}

float Subtract(float shape, float hole) {
    return std::max(shape, -hole);
}

// White, with the shape's coverage of each pixel as alpha: four samples a pixel, so thin parts stay smooth.
template <typename F>
Spot::Image Paint(std::uint32_t width, std::uint32_t height, F&& shape) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            float coverage = 0.0f;
            for (const float dy : {0.25f, 0.75f}) {
                for (const float dx : {0.25f, 0.75f}) {
                    const float distance = shape(Point{static_cast<float>(x) + dx, static_cast<float>(y) + dy});
                    coverage += std::clamp(0.5f - distance * 2.0f, 0.0f, 1.0f) * 0.25f;
                }
            }
            std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * width + x) * 4];
            pixel[0] = pixel[1] = pixel[2] = 255;
            pixel[3] = static_cast<std::uint8_t>(coverage * 255.0f + 0.5f);
        }
    }
    return {width, height, std::move(pixels)};
}

constexpr std::uint32_t ItemWidth = 256;
constexpr std::uint32_t ItemHeight = 128;
constexpr std::uint32_t SymbolSize = 128;

} // namespace

Spot::Image PhysicsGun() {
    return Paint(ItemWidth, ItemHeight, [](Point p) {
        const float body = Union({
            Polygon(p, {{36, 50}, {150, 42}, {168, 48}, {168, 82}, {150, 88}, {36, 80}}, 4.0f),
            Circle(p, {38, 65}, 18.0f),
            Polygon(p, {{84, 84}, {106, 84}, {98, 114}, {78, 112}}, 3.0f),
        });
        // The emitter's ring around its glowing core, held in two claws and a spike.
        const float ring = std::abs(Circle(p, {178, 65}, 15.0f)) - 4.5f;
        const float claws = Union({
            Polygon(p, {{166, 46}, {186, 38}, {244, 26}, {238, 36}, {190, 52}}, 1.5f),
            Polygon(p, {{166, 84}, {186, 92}, {244, 104}, {238, 94}, {190, 78}}, 1.5f),
            Capsule(p, {196, 65}, {232, 65}, 3.5f),
        });
        const float vents = Union({Box(p, {56, 54}, {62, 76}, 2.0f), Box(p, {70, 52}, {76, 78}, 2.0f)});
        return Union(
            {Subtract(Subtract(body, vents), Circle(p, {178, 65}, 11.0f)), ring, Circle(p, {178, 65}, 6.0f), claws});
    });
}

Spot::Image Pistol() {
    return Paint(ItemWidth, ItemHeight, [](Point p) {
        const float slide = Box(p, {66, 34}, {198, 58}, 3.0f);
        const float serrations =
            Union({Box(p, {76, 38}, {79, 54}), Box(p, {83, 38}, {86, 54}), Box(p, {90, 38}, {93, 54})});
        const float sights = Union({Box(p, {70, 28}, {82, 36}, 1.5f), Box(p, {186, 28}, {192, 36}, 1.0f)});
        const float frame = Box(p, {96, 56}, {196, 68}, 2.0f);
        const float grip = Polygon(p, {{72, 58}, {112, 58}, {102, 112}, {64, 108}}, 3.0f);
        const float guard = std::max(std::abs(Circle(p, {126, 70}, 13.0f)) - 2.5f, 66.0f - p.y);
        const float trigger = Capsule(p, {124, 64}, {120, 76}, 2.5f);
        return Union({Subtract(slide, serrations), sights, frame, grip, guard, trigger});
    });
}

Spot::Image Rifle() {
    return Paint(ItemWidth, ItemHeight, [](Point p) {
        const float stock = Polygon(p, {{6, 48}, {54, 48}, {60, 56}, {60, 68}, {14, 86}, {6, 86}}, 2.0f);
        const float receiver = Union({Box(p, {52, 50}, {70, 62}, 1.0f), Box(p, {66, 44}, {162, 66}, 2.0f)});
        const float rail = Union({Box(p, {82, 38}, {150, 44}, 1.0f), Box(p, {86, 30}, {100, 40}, 2.0f)});
        const float handguard =
            Subtract(Box(p, {158, 44}, {212, 64}, 3.0f),
                     Union({Box(p, {168, 49}, {178, 52}, 1.5f), Box(p, {186, 49}, {196, 52}, 1.5f),
                            Box(p, {168, 56}, {178, 59}, 1.5f), Box(p, {186, 56}, {196, 59}, 1.5f)}));
        const float barrel = Union({Box(p, {208, 50}, {238, 56}), Box(p, {234, 47}, {250, 59}, 1.5f),
                                    Polygon(p, {{198, 44}, {203, 30}, {209, 30}, {209, 44}}, 0.5f)});
        const float magazine = Polygon(p, {{112, 64}, {136, 64}, {146, 104}, {124, 110}}, 2.0f);
        const float grip = Polygon(p, {{80, 64}, {100, 64}, {92, 100}, {72, 97}}, 2.0f);
        const float guard = Capsule(p, {98, 73}, {112, 73}, 2.0f);
        return Union({stock, receiver, rail, handguard, barrel, magazine, grip, guard});
    });
}

Spot::Image Shotgun() {
    return Paint(ItemWidth, ItemHeight, [](Point p) {
        const float stock = Polygon(p, {{4, 54}, {60, 50}, {70, 58}, {72, 68}, {14, 88}, {4, 88}}, 2.5f);
        const float receiver = Box(p, {66, 46}, {132, 66}, 2.0f);
        const float barrel = Union({Box(p, {128, 47}, {248, 54}, 1.0f), Box(p, {240, 42}, {245, 47}, 1.0f)});
        const float tube = Box(p, {128, 57}, {228, 63}, 1.5f);
        const float pump =
            Subtract(Box(p, {150, 54}, {206, 69}, 3.5f),
                     Union({Box(p, {160, 66}, {163, 70}), Box(p, {168, 66}, {171, 70}), Box(p, {176, 66}, {179, 70}),
                            Box(p, {184, 66}, {187, 70}), Box(p, {192, 66}, {195, 70})}));
        const float guard = std::max(std::abs(Circle(p, {96, 66}, 12.0f)) - 2.5f, 64.0f - p.y);
        const float trigger = Capsule(p, {96, 64}, {92, 74}, 2.5f);
        return Union({stock, receiver, barrel, tube, pump, guard, trigger});
    });
}

Spot::Image RocketLauncher() {
    return Paint(ItemWidth, ItemHeight, [](Point p) {
        const float tube =
            Union({Box(p, {16, 48}, {226, 70}, 3.0f), Polygon(p, {{4, 42}, {22, 48}, {22, 70}, {4, 76}}, 1.0f),
                   Box(p, {218, 44}, {232, 74}, 2.0f)});
        const float bands = Union({Box(p, {60, 45}, {66, 73}, 1.0f), Box(p, {176, 45}, {182, 73}, 1.0f)});
        const float warhead = Polygon(p, {{232, 50}, {242, 51}, {253, 59}, {242, 67}, {232, 68}}, 1.0f);
        const float sight = Union({Box(p, {98, 36}, {128, 48}, 2.0f), Box(p, {106, 28}, {118, 38}, 1.5f)});
        const float grips = Union({Polygon(p, {{104, 70}, {120, 70}, {114, 100}, {98, 98}}, 2.0f),
                                   Polygon(p, {{150, 70}, {164, 70}, {162, 94}, {150, 94}}, 2.0f)});
        return Union({tube, bands, warhead, sight, grips});
    });
}

Spot::Image ToolGun() {
    return Paint(ItemWidth, ItemHeight, [](Point p) {
        const float body = Subtract(Box(p, {38, 38}, {178, 80}, 5.0f), Box(p, {56, 46}, {116, 70}, 3.0f));
        const float screen = Box(p, {61, 51}, {111, 65}, 1.5f);
        const float nozzle = Union({Box(p, {176, 50}, {214, 68}, 2.0f), Box(p, {212, 45}, {226, 73}, 2.0f),
                                    Box(p, {226, 54}, {238, 64}, 1.5f)});
        const float fin = Box(p, {128, 30}, {164, 40}, 2.0f);
        const float grip = Polygon(p, {{70, 78}, {96, 78}, {88, 114}, {62, 111}}, 3.0f);
        const float dial = Circle(p, {146, 59}, 7.0f);
        return Union({Subtract(body, dial), Circle(p, {146, 59}, 3.5f), screen, nozzle, fin, grip});
    });
}

Spot::Image Sun() {
    return Paint(SymbolSize, SymbolSize, [](Point p) {
        constexpr float Diagonal = 0.70710678f;
        constexpr Point Center{64, 64};
        float distance = Circle(p, Center, 25.0f);
        const Point directions[] = {{1, 0},  {Diagonal, Diagonal},   {0, 1},  {-Diagonal, Diagonal},
                                    {-1, 0}, {-Diagonal, -Diagonal}, {0, -1}, {Diagonal, -Diagonal}};
        for (const Point d : directions) {
            distance = std::min(distance, Capsule(p, {Center.x + d.x * 37.0f, Center.y + d.y * 37.0f},
                                                  {Center.x + d.x * 52.0f, Center.y + d.y * 52.0f}, 5.0f));
        }
        return distance;
    });
}

Spot::Image Moon() {
    return Paint(SymbolSize, SymbolSize,
                 [](Point p) { return Subtract(Circle(p, {62, 66}, 44.0f), Circle(p, {88, 46}, 38.0f)); });
}

Spot::Image Health() {
    return Paint(SymbolSize, SymbolSize,
                 [](Point p) { return Union({Box(p, {48, 14}, {80, 114}, 7.0f), Box(p, {14, 48}, {114, 80}, 7.0f)}); });
}

Spot::Image WheelSector() {
    constexpr std::uint32_t Size = 256;
    return Paint(Size, Size, [](Point p) {
        // Sines of the half of an eighth of a turn, the sector's half angle.
        constexpr float Cos = 0.92387953f;
        constexpr float Sin = 0.38268343f;
        const Point local{p.x - 128.0f, 260.0f - p.y};
        const float radius = Length(local);
        const float side = std::abs(local.x) * Cos - local.y * Sin + 3.0f;
        return std::max({side, radius - 252.0f, 118.0f - radius});
    });
}

Spot::Image Vignette() {
    constexpr std::uint32_t Size = 256;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(Size) * Size * 4);
    for (std::uint32_t y = 0; y < Size; ++y) {
        for (std::uint32_t x = 0; x < Size; ++x) {
            // From 0 at the middle to 1 at the middle of each edge.
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(Size) * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(Size) * 2.0f - 1.0f;
            const float t = std::clamp((std::sqrt(u * u + v * v) - 0.8f) / 0.62f, 0.0f, 1.0f);
            std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * Size + x) * 4];
            pixel[0] = pixel[1] = pixel[2] = 255;
            pixel[3] = static_cast<std::uint8_t>(t * t * 255.0f + 0.5f);
        }
    }
    return {Size, Size, std::move(pixels)};
}

} // namespace Generator::Icons
