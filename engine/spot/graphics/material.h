#pragma once

#include "spot/assets/asset_ref.h"
#include "spot/math/color.h"

namespace Spot {

class Texture;
class Archive;

// How a material's alpha, the base color's times its texture's, covers what is behind it.
enum class AlphaMode {
    // Alpha is ignored: the surface hides what is behind it.
    Opaque,
    // Pixels whose alpha is below the cutoff are not drawn, nor cast shadows, and the others are opaque: leaves,
    // fences and grates, with hard edges.
    Cutout,
    // The surface covers what is behind it as much as its alpha says, and reflects light at full strength over it, as
    // glass does. Transparent surfaces are drawn after the opaque ones, from the farthest to the nearest, and do not
    // hide what is drawn after them or cast shadows.
    Transparent,
};

// How Renderer3D shades a mesh: metallic-roughness PBR, as glTF defines it. Textures are optional; each multiplies
// the value it goes with. Color textures (base color and emissive) are sRGB, and the others hold data, so they are
// imported with their texture's sRGB setting off.
struct Material {
    // The color of the surface: what it scatters for dielectrics, and what it reflects for metals.
    Color baseColor = Color::White;
    AssetRef<Texture> baseColorTexture = {};
    // 0 for dielectrics (wood, plastic, stone), 1 for metals; in between only where the two blend, such as at the
    // edge of rust.
    float metallic = 0.0f;
    // From 0 for a mirror to 1 for a matte surface, whose highlights spread over its whole lit side. Matte by default,
    // as in glTF.
    float roughness = 1.0f;
    // Roughness in its green channel and metallic in its blue one, as glTF packs them.
    AssetRef<Texture> metallicRoughnessTexture = {};
    // A tangent-space normal map, as glTF and MikkTSpace bake them (+Y toward the texture's top); without one, the
    // surface follows the mesh's normals.
    AssetRef<Texture> normalTexture = {};
    // How strongly the normal map tilts the surface: 0 flattens it, above 1 exaggerates it.
    float normalScale = 1.0f;
    // How much ambient light reaches each point, in its red channel, darkening creases.
    AssetRef<Texture> occlusionTexture = {};
    // How much the occlusion map darkens, from 0 (not at all) to 1.
    float occlusionStrength = 1.0f;
    // Light the surface gives off itself, added to what lights it, as screens and lamps do. Linear; above 1, it glows
    // through bloom. It lights nothing else.
    Color emissive = Color::Black;
    AssetRef<Texture> emissiveTexture = {};
    AlphaMode alphaMode = AlphaMode::Opaque;
    // The alpha below which cutout pixels are not drawn.
    float alphaCutoff = 0.5f;
    // Draws back faces too, for thin surfaces such as leaves or cloth.
    bool doubleSided = false;
    // When positive, the textures are projected onto the surface from world space instead of following the mesh's
    // UVs, repeating every this many units, so they keep their size however the mesh is scaled. Suits
    // BuiltInAssets::GridTexture on level blocks.
    float worldTextureSize = 0.0f;
    // Samples exact texels for pixel art, overriding the renderer's default filtering in all passes.
    bool nearestFiltering = false;

    void Serialize(Archive& archive);
    friend bool operator==(const Material&, const Material&) = default;
};

} // namespace Spot
