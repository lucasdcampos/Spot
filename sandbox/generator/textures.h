#pragma once

#include <spot.h>

#include <array>
#include <string_view>

// The props' base color textures, painted from code with fixed seeds, so every run paints the same pixels. Round
// props' textures follow the layout of Generator::Shapes: the side in the top half, the ends as discs below.
namespace Generator::Textures {

inline constexpr std::uint32_t Size = 256;

Spot::Image Crate();
Spot::Image Barrel();
Spot::Image ExplosiveBarrel();
// Tiles in both directions, with the grain along X.
Spot::Image Plank();
// Tiles in both directions, brushed along X, with rivets along the top and bottom.
Spot::Image Beam();
// For a sphere's UVs: colored slices around it, white at the poles.
Spot::Image Ball();
Spot::Image Cone();
// Tiles in both directions.
Spot::Image Concrete();
Spot::Image Wheel();
Spot::Image TargetDummy();

// The interface's images, 64 pixels square, white with smooth edges in their alpha, for tinting: a rounded panel to
// nine-slice with borders of 12 pixels, a dot, and the cross of a hit marker.
Spot::Image UiPanel();
Spot::Image UiDot();
Spot::Image UiHitMarker();

// The surfaces props, weapons and the level are made of, each a set of tiling PBR textures.
enum class Surface { Wood, PaintedMetal, RustyMetal, Concrete, Rubber, Glass };

inline constexpr std::array<Surface, 6> Surfaces = {Surface::Wood,     Surface::PaintedMetal, Surface::RustyMetal,
                                                    Surface::Concrete, Surface::Rubber,       Surface::Glass};

// Names as in file names, such as "painted_metal".
std::string_view GetSurfaceName(Surface surface);

// A surface's textures, as glTF materials take them: its color (sRGB), its tangent-space normals (+Y toward the
// texture's top), and its occlusion, roughness and metallic in red, green and blue, which serves as both the
// occlusion and the metallic-roughness texture.
struct SurfaceTextures {
    Spot::Image color;
    Spot::Image normal;
    Spot::Image orm;
};

SurfaceTextures PaintSurface(Surface surface);

} // namespace Generator::Textures
