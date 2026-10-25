#pragma once

#include <spot.h>

#include <array>
#include <cstdint>
#include <string_view>

// The textures the generator paints from code with fixed seeds, so every run paints the same pixels: the interface's
// images and the surfaces that props, weapons and the level are made of. The props' own are in prop_textures.h.
namespace Generator::Textures {

// The interface's images, 64 pixels square, white with smooth edges in their alpha, for tinting: a panel with corners
// rounded by 4 pixels to nine-slice with borders of 6 pixels, its 1 pixel outline to lay over it, the soft shadow to
// lay under it, 16 pixels wider on each side, to nine-slice with borders of 24, a dot, and the cross of a hit marker.
Spot::Image UiPanel();
Spot::Image UiOutline();
Spot::Image UiShadow();
Spot::Image UiDot();
Spot::Image UiHitMarker();
// Opaque in the middle and fading out to its edges, 128 pixels square: tinted dark and stretched behind the HUD's
// corners, it keeps their text legible over a bright sky without a box around it.
Spot::Image UiScrim();
// Galvanized chain link: wires along both diagonals, white in color and alpha, for cutout fences.
Spot::Image ChainLink();

// The surfaces weapons, the level and some props are made of, each a set of tiling PBR textures, but for leaves and
// needles: a twig of them on its own, cut out by its alpha, for the cards trees' foliage is made of. Polymer, gunmetal
// and walnut are the weapons' own.
enum class Surface {
    Wood,
    PaintedMetal,
    RustyMetal,
    Concrete,
    Rubber,
    Glass,
    Grass,
    Gravel,
    Asphalt,
    Paving,
    Brick,
    Corrugated,
    TreadPlate,
    Rock,
    Bark,
    Leaves,
    Water,
    Slab,
    Needles,
    Polymer,
    Gunmetal,
    Walnut,
};

inline constexpr std::array<Surface, 22> Surfaces = {
    Surface::Wood,     Surface::PaintedMetal, Surface::RustyMetal, Surface::Concrete, Surface::Rubber,
    Surface::Glass,    Surface::Grass,        Surface::Gravel,     Surface::Asphalt,  Surface::Paving,
    Surface::Brick,    Surface::Corrugated,   Surface::TreadPlate, Surface::Rock,     Surface::Bark,
    Surface::Leaves,   Surface::Water,        Surface::Slab,       Surface::Needles,  Surface::Polymer,
    Surface::Gunmetal, Surface::Walnut};

// Names as in file names, such as "painted_metal".
std::string_view GetSurfaceName(Surface surface);
// How many pixels square its textures are: 2048 for grass and concrete slabs, which cover most of the ground and are
// seen closest, 1024 for what is seen up close, as weapons and props are, and less for the rest.
std::uint32_t GetSurfaceSize(Surface surface);

// A surface's textures, as glTF materials take them: its color (sRGB), its tangent-space normals (+Y toward the
// texture's top), and its occlusion, roughness and metallic in red, green and blue, which serves as both the
// occlusion and the metallic-roughness texture.
struct SurfaceTextures {
    Spot::Image color;
    Spot::Image normal;
    Spot::Image orm;
};

// Paints the surface size pixels square, GetSurfaceSize() as it is meant to be.
SurfaceTextures PaintSurface(Surface surface, std::uint32_t size);

} // namespace Generator::Textures
