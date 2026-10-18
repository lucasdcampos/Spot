#include "effect_textures.h"

#include "texture_painting.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace Generator::EffectTextures {

namespace {

using Textures::Fractal;
using Textures::Smooth;

struct Texel {
    // Grey, as particles tint it.
    float shade = 1.0f;
    float alpha = 0.0f;
};

// Paints from the texture's coordinates, from 0 to 1 at the pixels' centers, y down.
template <typename F>
Spot::Image Paint(std::uint32_t width, std::uint32_t height, F&& texel) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    const auto byte = [](float value) {
        return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const Texel value = texel((static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                                      (static_cast<float>(y) + 0.5f) / static_cast<float>(height));
            std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * width + x) * 4];
            pixel[0] = pixel[1] = pixel[2] = byte(value.shade);
            pixel[3] = byte(value.alpha);
        }
    }
    return {width, height, std::move(pixels)};
}

// Around the center, from -1 to 1 across the texture, y down.
struct Polar {
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
    // The direction's cosine and sine, from which the multiple angles' come without trigonometry.
    float cosine = 1.0f;
    float sine = 0.0f;
};

Polar ToPolar(float u, float v) {
    Polar polar{u * 2.0f - 1.0f, v * 2.0f - 1.0f};
    polar.radius = std::sqrt(polar.x * polar.x + polar.y * polar.y);
    if (polar.radius > 1e-5f) {
        polar.cosine = polar.x / polar.radius;
        polar.sine = polar.y / polar.radius;
    }
    return polar;
}

// The cosines of three and five times the angle (Chebyshev's polynomials), and the sines of two and three times it.
float Cos3(const Polar& p) {
    return p.cosine * (4.0f * p.cosine * p.cosine - 3.0f);
}

float Cos5(const Polar& p) {
    const float c2 = p.cosine * p.cosine;
    return p.cosine * (16.0f * c2 * c2 - 20.0f * c2 + 5.0f);
}

float Sin2(const Polar& p) {
    return 2.0f * p.sine * p.cosine;
}

float Sin3(const Polar& p) {
    return p.sine * (3.0f - 4.0f * p.sine * p.sine);
}

// Nothing reaches the texture's edge, so no particle shows its square.
float EdgeFade(const Polar& p) {
    return Smooth(1.0f, 0.88f, p.radius);
}

} // namespace

Spot::Image Smoke() {
    struct Lobe {
        float x;
        float y;
        float radius;
    };
    // A cauliflower of overlapping puffs, rounder at the top, as smoke rolls up.
    static constexpr Lobe Lobes[] = {{0.0f, 0.08f, 0.55f},    {-0.36f, 0.16f, 0.38f}, {0.34f, 0.12f, 0.4f},
                                     {-0.14f, -0.28f, 0.42f}, {0.22f, -0.26f, 0.36f}, {0.02f, 0.38f, 0.34f},
                                     {-0.42f, -0.08f, 0.3f},  {0.45f, -0.05f, 0.28f}};
    return Paint(256, 256, [](float u, float v) {
        const Polar p = ToPolar(u, v);
        float field = -1.0f;
        for (const Lobe& lobe : Lobes) {
            const float dx = p.x - lobe.x;
            const float dy = p.y - lobe.y;
            field = std::max(field, 1.0f - std::sqrt(dx * dx + dy * dy) / lobe.radius);
        }
        const float billow = Fractal(u, v, 4, 4, 5, 9101);
        field += (billow - 0.5f) * 0.5f;
        const float density = 0.7f + 0.3f * Fractal(u, v, 8, 8, 3, 9107);
        const float detail = Fractal(u, v, 8, 8, 4, 9113);
        // Lit from above, darker below and in the folds between the puffs.
        const float light = 0.5f + 0.5f * Smooth(0.85f, -0.85f, p.y + (billow - 0.5f) * 0.7f);
        const float folds = 0.78f + 0.22f * Smooth(0.0f, 0.45f, field) + 0.18f * (detail - 0.5f);
        return Texel{light * folds, Smooth(0.0f, 0.38f, field) * density * EdgeFade(p)};
    });
}

Spot::Image Fire() {
    return Paint(256, 256, [](float u, float v) {
        const Polar p = ToPolar(u, v);
        // Warped, so it is ragged rather than round.
        const float wx = p.x + (Fractal(u, v, 3, 3, 5, 9201) - 0.5f) * 0.6f;
        const float wy = p.y + (Fractal(u, v, 3, 3, 5, 9211) - 0.5f) * 0.6f;
        const float warped = std::sqrt(wx * wx + wy * wy);
        const float body = Smooth(0.95f, 0.3f, warped);
        // Ridges of the noise are the bright filaments of burning gas.
        const float ridge = 1.0f - std::abs(Fractal(u, v, 5, 5, 4, 9221) * 2.0f - 1.0f);
        const float filaments = Smooth(0.35f, 0.95f, ridge);
        const float core = Smooth(0.75f, 0.0f, warped);
        return Texel{0.4f + 0.45f * core + 0.35f * filaments, body * (0.45f + 0.55f * filaments) * EdgeFade(p)};
    });
}

Spot::Image MuzzleFlash() {
    return Paint(128, 128, [](float u, float v) {
        const Polar p = ToPolar(u, v);
        const float petal = std::max(Cos5(p), 0.0f);
        const float sharp = petal * petal;
        // Uneven petals, ragged at their tips.
        const float length = 0.62f + 0.16f * Cos3(p) + 0.1f * Sin2(p);
        const float ragged = 0.85f + 0.3f * Fractal(u, v, 6, 6, 3, 9301);
        const float reach = (0.2f + sharp * length) * ragged;
        const float flame = Smooth(reach, reach * 0.5f, p.radius);
        const float core = Smooth(0.38f, 0.0f, p.radius);
        return Texel{0.72f + 0.28f * core, std::max(flame * 0.85f, core) * EdgeFade(p)};
    });
}

Spot::Image Flame() {
    return Paint(64, 128, [](float u, float v) {
        // Widest a little behind its middle, tapering to a point at each end.
        const float width = 0.46f * std::sqrt(std::max(4.0f * v * (1.0f - v), 0.0f)) * (0.7f + 0.3f * v);
        const float sway = (Fractal(u, v, 2, 4, 4, 9401) - 0.5f) * 0.18f;
        const float across = std::abs(u - 0.5f + sway);
        const float body = Smooth(width, width * 0.35f, across);
        const float tongues = Fractal(u, v, 4, 8, 4, 9411);
        const float inner = Smooth(width * 0.7f, 0.0f, across);
        return Texel{0.7f + 0.3f * inner, body * (0.55f + 0.45f * tongues)};
    });
}

Spot::Image Debris() {
    return Paint(64, 64, [](float u, float v) {
        const Polar p = ToPolar(u, v);
        const float reach =
            0.6f + 0.13f * Cos3(p) + 0.1f * Sin3(p) + 0.08f * Sin2(p) + (Fractal(u, v, 4, 4, 3, 9501) - 0.5f) * 0.25f;
        const float coverage = Smooth(reach, reach - 0.05f, p.radius);
        const float light = 0.35f + 0.65f * Smooth(0.7f, -0.7f, p.y) * (0.7f + 0.3f * Fractal(u, v, 8, 8, 3, 9511));
        return Texel{light, coverage * EdgeFade(p)};
    });
}

Spot::Image Ripple() {
    return Paint(256, 256, [](float u, float v) {
        const Polar p = ToPolar(u, v);
        const auto ring = [&](float radius, float width) { return Smooth(width, 0.0f, std::abs(p.radius - radius)); };
        const float rings = 0.9f * ring(0.86f, 0.05f) + 0.5f * ring(0.7f, 0.045f) + 0.28f * ring(0.55f, 0.04f);
        // Broken up a little, as wind and the water's own waves break them.
        const float broken = 0.7f + 0.3f * Fractal(u, v, 8, 8, 3, 9601);
        return Texel{1.0f, rings * broken * EdgeFade(p)};
    });
}

} // namespace Generator::EffectTextures
