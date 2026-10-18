#include "level_textures.h"

#include "foliage_textures.h"
#include "texture_painting.h"

namespace Generator::Textures {

namespace {

// The texture's coordinates, from 0 to 1, at the pixel's center, for a texture of the size given by the scale of a
// 256-pixel one's pixels.
float Coordinate(int pixel, float scale) {
    return (static_cast<float>(pixel) + 0.5f) * scale / 256.0f;
}

// Gravel: pebbles of several sizes and stones (greys, buff and brown) packed together, each rounded on top, with
// dark grit showing between them.
SurfacePoint GravelPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float grain = Random(x, y, 1101);
    // Warping the cells' space a little rounds their straight edges off, as pebbles are.
    const float warpU = (Fractal(u, v, 24, 24, 2, 1102) - 0.5f) * 0.5f;
    const float warpV = (Fractal(u, v, 24, 24, 2, 1103) - 0.5f) * 0.5f;
    const CellSample large = Cells(u * 14.0f + warpU, v * 14.0f + warpV, 14, 1104);
    const CellSample small = Cells(u * 34.0f + warpU * 2.0f, v * 34.0f + warpV * 2.0f, 34, 1105);
    // How far inside its cell a point is: 0 at the gaps between stones, rising to their tops.
    const float largeInside = Smooth(0.04f, 0.45f, large.second - large.nearest);
    const float smallInside = Smooth(0.04f, 0.4f, small.second - small.nearest);
    // Some large cells hold a stone; elsewhere small pebbles fill in.
    const bool isLarge = large.id > 0.35f && largeInside > 0.0f;
    const float inside = isLarge ? largeInside : smallInside;
    const float id = isLarge ? large.id : small.id;
    const float tint = Random(static_cast<int>(id * 65536.0f), 0, 1106);
    Rgb stone = Mix(Rgb{0.42f, 0.41f, 0.39f}, Rgb{0.63f, 0.59f, 0.52f}, id);
    stone = Mix(stone, Rgb{0.45f, 0.36f, 0.27f}, tint > 0.75f ? 0.7f : 0.0f);
    stone = stone * (0.82f + grain * 0.12f + std::sqrt(inside) * 0.12f);
    const Rgb grit = Mix(Rgb{0.17f, 0.14f, 0.11f}, Rgb{0.3f, 0.26f, 0.21f}, grain);
    const float covered = Smooth(0.0f, 0.25f, inside);
    return {.color = Mix(grit, stone, covered),
            .height = std::sqrt(inside) * (isLarge ? 6.0f : 3.0f) + grain * 0.3f,
            .occlusion = 0.35f + covered * 0.65f,
            .roughness = 0.94f - covered * 0.2f + grain * 0.04f};
}

// Worn asphalt: dark binder with small light stones, smoother where tires polish it, and a few fine cracks.
SurfacePoint AsphaltPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float wear = Fractal(u, v, 4, 4, 4, 1201);
    const float grain = Random(x, y, 1202);
    const CellSample aggregate = Cells(u * 110.0f, v * 110.0f, 110, 1203);
    const float radius = 0.18f + aggregate.id * 0.15f;
    const float stone = Smooth(radius, radius * 0.4f, aggregate.nearest) * Smooth(0.45f, 0.6f, aggregate.id);
    const CellSample plates = Cells(u * 5.0f, v * 5.0f, 5, 1204);
    const float crack = 1.0f - Smooth(0.0f, 0.02f, plates.second - plates.nearest);
    const float cracked = crack * Smooth(0.62f, 0.78f, Fractal(u, v, 3, 3, 2, 1205));
    Rgb color = Rgb{0.19f, 0.19f, 0.2f} * (0.82f + wear * 0.3f + grain * 0.12f);
    color = Mix(color, Rgb{0.4f, 0.39f, 0.37f} * (0.85f + aggregate.id * 0.25f), stone * 0.45f);
    color = Mix(color, Rgb{0.08f, 0.08f, 0.085f}, cracked * 0.8f);
    return {.color = color,
            .height = stone * 1.0f + grain * 0.5f - cracked * 1.5f,
            .occlusion = 1.0f - cracked * 0.4f,
            .roughness = 0.86f - wear * 0.16f - stone * 0.08f};
}

// Square concrete pavers, four by four, each a slightly different shade, with chamfered edges and sandy joints.
SurfacePoint PavingPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    constexpr int Tiles = 4;
    const float tu = u * Tiles;
    const float tv = v * Tiles;
    const int column = static_cast<int>(std::floor(tu));
    const int row = static_cast<int>(std::floor(tv));
    // How far from the paver's edge, in its own size.
    const float edge = std::min({tu - std::floor(tu), std::ceil(tu) - tu, tv - std::floor(tv), std::ceil(tv) - tv});
    const float shade = Random(column, row, 1301);
    const float speckle = Random(x, y, 1302);
    const float stain = Fractal(u, v, 4, 4, 4, 1303);
    const float pores = Fractal(u, v, 48, 48, 2, 1304);
    if (edge < 0.025f) {
        // The joint, filled with sand.
        return {.color = Rgb{0.36f, 0.33f, 0.28f} * (0.85f + speckle * 0.2f),
                .height = -2.5f,
                .occlusion = 0.55f,
                .roughness = 0.95f};
    }
    const float chamfer = Smooth(0.025f, 0.06f, edge);
    Rgb color = Mix(Rgb{0.55f, 0.54f, 0.51f}, Rgb{0.68f, 0.66f, 0.62f}, shade);
    color = color * (0.86f + pores * 0.16f + speckle * 0.05f) * (0.9f + stain * 0.15f);
    return {.color = color,
            .height = chamfer * 2.5f - 2.5f + pores * 0.5f,
            .occlusion = 0.75f + chamfer * 0.25f,
            .roughness = 0.72f + pores * 0.15f + (1.0f - chamfer) * 0.1f};
}

// Red bricks in running bond, each its own shade and slightly worn, in recessed mortar.
SurfacePoint BrickPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    constexpr int Rows = 10;
    constexpr int Columns = 4;
    const float bv = v * Rows;
    const int row = static_cast<int>(std::floor(bv));
    // Every other row is offset by half a brick.
    const float bu = u * Columns + (row % 2 == 0 ? 0.0f : 0.5f);
    const int column = Wrap(static_cast<int>(std::floor(bu)), Columns);
    const float fu = bu - std::floor(bu);
    const float fv = bv - std::floor(bv);
    // The mortar's width in each direction, in bricks, and how far in from it the brick's edge is rounded.
    constexpr float MortarU = 0.03f;
    constexpr float MortarV = 0.08f;
    const float edge =
        std::min({fu - MortarU, 1.0f - MortarU - fu, (fv - MortarV) * 0.4f, (1.0f - MortarV - fv) * 0.4f});
    const float speckle = Random(x, y, 1401);
    const float grime = Fractal(u, v, 3, 3, 4, 1402);
    if (edge < 0.0f) {
        return {.color = Rgb{0.62f, 0.6f, 0.56f} * (0.8f + speckle * 0.2f) * (0.85f + grime * 0.2f),
                .height = -3.0f + speckle * 0.4f,
                .occlusion = 0.55f,
                .roughness = 0.95f};
    }
    const float bevel = Smooth(0.0f, 0.04f, edge);
    const float shade = Random(column, row, 1403);
    const float burn = Random(column, row, 1404);
    const float surface = Fractal(u, v, 32, 64, 3, 1405 + static_cast<std::uint32_t>(row));
    const float chips = Smooth(0.7f, 0.8f, Fractal(u, v, 16, 40, 3, 1406));
    Rgb color = Mix(Rgb{0.5f, 0.2f, 0.13f}, Rgb{0.66f, 0.33f, 0.22f}, shade);
    color = Mix(color, Rgb{0.32f, 0.15f, 0.11f}, burn * burn * 0.6f);
    color = color * (0.85f + surface * 0.25f + speckle * 0.06f) * (0.88f + grime * 0.15f) * (1.0f - chips * 0.2f);
    return {.color = color,
            .height = bevel * 2.0f - 2.0f + surface * 0.8f - chips * 0.8f,
            .occlusion = 0.7f + bevel * 0.3f,
            .roughness = 0.82f + surface * 0.12f};
}

// Painted corrugated steel sheet, its ridges along v, streaked by rain and rusting where the paint gave out.
SurfacePoint CorrugatedPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    constexpr float Ridges = 8.0f;
    const float ridge = Wave(u * Ridges);
    const float streaks = Fractal(u, v, 24, 2, 3, 1501);
    const float rust = Smooth(0.68f, 0.82f, Fractal(u, v, 6, 6, 4, 1502));
    const float speckle = Random(x, y, 1503);
    Rgb paint = Rgb{0.86f, 0.87f, 0.86f} * (0.82f + streaks * 0.22f + speckle * 0.04f);
    // Dirt gathers in the troughs.
    paint = paint * (0.92f + ridge * 0.08f);
    const Rgb rusted = Mix(Rgb{0.45f, 0.22f, 0.1f}, Rgb{0.6f, 0.33f, 0.15f}, speckle);
    return {.color = Mix(paint, rusted, rust),
            .height = ridge * 7.0f + rust * speckle * 0.6f,
            .occlusion = 0.82f + ridge * 0.18f,
            .roughness = 0.42f + streaks * 0.12f + rust * 0.4f};
}

// Steel tread plate: raised diamond lugs, alternating in direction, on brushed and scuffed steel.
SurfacePoint TreadPlatePoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    constexpr int Lugs = 8;
    const float lu = u * Lugs;
    const float lv = v * Lugs;
    const int column = static_cast<int>(std::floor(lu));
    const int row = static_cast<int>(std::floor(lv));
    // From the lug's center, turned 45 degrees one way or the other.
    const float cu = lu - std::floor(lu) - 0.5f;
    const float cv = lv - std::floor(lv) - 0.5f;
    const float sign = (column + row) % 2 == 0 ? 1.0f : -1.0f;
    const float along = (cu + cv * sign) * 0.7071f;
    const float across = (cu * sign - cv) * 0.7071f;
    const float lug = 1.0f - std::sqrt(along * along / 0.1f + across * across / 0.006f);
    const float raised = Smooth(0.0f, 0.25f, lug);
    const float brushed = Fractal(u, v, 2, 96, 3, 1601);
    const float scuffs = Fractal(u, v, 8, 8, 4, 1602);
    const Rgb steel = Rgb{0.6f, 0.61f, 0.63f} * (0.85f + brushed * 0.15f);
    return {.color = steel * (0.92f + raised * 0.12f) * (0.85f + scuffs * 0.2f),
            .height = raised * 3.0f + brushed * 0.3f,
            .occlusion = 0.8f + raised * 0.2f,
            .roughness = 0.5f - raised * 0.2f + scuffs * 0.15f,
            .metallic = 1.0f};
}

// A poured concrete slab: troweled, stained in broad patches, with fine pores and a saw-cut joint along two of its
// edges, so slabs tile into a grid.
SurfacePoint SlabPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float stains = Fractal(u, v, 3, 3, 5, 2101);
    const float trowel = Fractal(u, v, 12, 12, 3, 2102);
    const float pores = Random(x, y, 2103);
    const float joint = std::min(u, v) * 256.0f / scale;
    Rgb color = Rgb{0.6f, 0.6f, 0.58f} * (0.82f + stains * 0.25f + trowel * 0.06f + pores * 0.04f);
    if (joint < 1.5f) {
        return {.color = color * 0.55f, .height = -2.0f, .occlusion = 0.6f, .roughness = 0.95f};
    }
    return {.color = color,
            .height = trowel * 0.8f + pores * 0.3f,
            .occlusion = 0.9f + stains * 0.1f,
            .roughness = 0.68f + trowel * 0.15f + pores * 0.08f - stains * 0.08f};
}

// Weathered rock: strata, a few deep cracks and patches of lichen.
SurfacePoint RockPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float body = Fractal(u, v, 4, 4, 6, 1701);
    const float detail = Fractal(u, v, 24, 24, 3, 1705);
    const float layers = Wave(v * 5.0f + body * 2.0f) * 0.5f + 0.5f;
    // Cracks wander: the cells are warped by noise that tiles as they do.
    const float warpU = (Fractal(u, v, 6, 6, 3, 1707) - 0.5f) * 1.2f;
    const float warpV = (Fractal(u, v, 6, 6, 3, 1708) - 0.5f) * 1.2f;
    const CellSample fractures = Cells(u * 4.0f + warpU, v * 4.0f + warpV, 4, 1702);
    // Only some of the edges between cells are cracks, and they thin out along their length.
    const float crack = (1.0f - Smooth(0.0f, 0.03f + detail * 0.02f, fractures.second - fractures.nearest)) *
                        Smooth(0.45f, 0.6f, Fractal(u, v, 5, 5, 3, 1706));
    const float lichen = Smooth(0.64f, 0.76f, Fractal(u, v, 8, 8, 4, 1703));
    const float grain = Random(x, y, 1704);
    // Sharp ridges where the surface broke off, and the specks of its minerals, light and dark.
    const float ridges = 1.0f - std::abs(Fractal(u, v, 10, 10, 4, 1709) * 2.0f - 1.0f);
    const float minerals = ValueNoise(u * 160.0f, v * 160.0f, 160, 160, 1710);
    const float speck = Smooth(0.75f, 0.85f, minerals) - Smooth(0.25f, 0.15f, minerals);
    Rgb color = Mix(Rgb{0.33f, 0.31f, 0.29f}, Rgb{0.58f, 0.55f, 0.5f}, body);
    color = color * (0.8f + layers * 0.14f + detail * 0.12f + ridges * 0.12f + grain * 0.05f + speck * 0.12f);
    color = Mix(color, Rgb{0.44f, 0.47f, 0.26f}, lichen * 0.55f);
    color = Mix(color, Rgb{0.13f, 0.12f, 0.11f}, crack * 0.85f);
    return {.color = color,
            .height =
                body * 10.0f + layers * 2.0f + detail * 2.0f + ridges * ridges * 3.0f - crack * 4.0f + grain * 0.4f,
            .occlusion = 1.0f - crack * 0.5f - (1.0f - body) * 0.2f,
            .roughness = 0.86f + grain * 0.08f - lichen * 0.05f};
}

// Furrowed bark, as an old oak's or pine's: long rounded plates along v, split by deep furrows that wander and join,
// grey-brown on their worn faces with a little lichen and dark in the furrows. Its v runs along the branch, about a
// meter per repeat.
SurfacePoint BarkPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    // Plates are cells four times as long as they are wide, their edges warped so the furrows wander.
    const float warpU = (Fractal(u, v, 4, 8, 3, 1801) - 0.5f) * 0.6f;
    const float warpV = (Fractal(u, v, 4, 8, 3, 1802) - 0.5f) * 0.6f;
    const CellSample plates = Cells(u * 12.0f + warpU, v * 3.0f + warpV, 12, 3, 1803);
    const float gap = plates.second - plates.nearest;
    // Each plate is rounded, worn smooth on top and falling away into the furrows around it.
    const float raised = Smooth(0.02f, 0.45f, gap);
    const float fibers = Fractal(u, v, 64, 8, 3, 1805);
    const float grain = Random(x, y, 1806);
    const float lichen = Smooth(0.62f, 0.78f, Fractal(u, v, 5, 5, 3, 1807)) * raised;
    Rgb face = Mix(Rgb{0.27f, 0.22f, 0.17f}, Rgb{0.44f, 0.39f, 0.33f}, plates.id * 0.6f + fibers * 0.4f);
    face = face * (0.7f + raised * 0.25f + fibers * 0.2f + grain * 0.08f);
    face = Mix(face, Rgb{0.46f, 0.49f, 0.37f}, lichen * 0.45f);
    const Rgb furrow = Rgb{0.12f, 0.09f, 0.07f} * (0.8f + fibers * 0.4f);
    return {.color = Mix(furrow, face, Smooth(0.0f, 0.35f, raised)),
            .height = raised * 10.0f + fibers * 1.5f + grain * 0.3f,
            .occlusion = 0.35f + raised * 0.65f,
            .roughness = 0.88f + grain * 0.08f - lichen * 0.05f};
}

// Calm water: gentle swells and ripples in its normals, smooth enough to mirror the sky.
SurfacePoint WaterPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float swell = Fractal(u, v, 3, 3, 3, 2001);
    // Wind ripples, longer across the wind than along it, wandering with the swell.
    const float ripples = Fractal(u + swell * 0.05f, v, 10, 18, 3, 2003);
    const float fine = Fractal(u, v, 24, 24, 2, 2002);
    return {.color = Mix(Rgb{0.08f, 0.2f, 0.22f}, Rgb{0.12f, 0.28f, 0.3f}, swell),
            .height = swell * 5.0f + ripples * 2.0f + fine * 0.5f,
            .roughness = 0.03f + fine * 0.03f};
}

} // namespace

SurfaceTextures PaintLevelSurface(Surface surface, std::uint32_t size) {
    switch (surface) {
    case Surface::Grass:
        return PaintGrass(size);
    case Surface::Gravel:
        return PaintSurfaceTexels(size, GravelPoint);
    case Surface::Asphalt:
        return PaintSurfaceTexels(size, AsphaltPoint);
    case Surface::Paving:
        return PaintSurfaceTexels(size, PavingPoint);
    case Surface::Brick:
        return PaintSurfaceTexels(size, BrickPoint);
    case Surface::Corrugated:
        return PaintSurfaceTexels(size, CorrugatedPoint);
    case Surface::TreadPlate:
        return PaintSurfaceTexels(size, TreadPlatePoint);
    case Surface::Rock:
        return PaintSurfaceTexels(size, RockPoint);
    case Surface::Bark:
        return PaintSurfaceTexels(size, BarkPoint);
    case Surface::Leaves:
        return PaintLeaves(size);
    case Surface::Needles:
        return PaintNeedles(size);
    case Surface::Slab:
        return PaintSurfaceTexels(size, SlabPoint);
    default:
        break;
    }
    return PaintSurfaceTexels(size, WaterPoint);
}

} // namespace Generator::Textures
