#pragma once

// Engine-internal: what the parts of Renderer3D share. Renderer3D's public header holds only an Impl, which owns a
// Renderer3DState and the parts that draw from it: MeshBatcher (renderer_3d_meshes.h), ShadowRenderer
// (renderer_3d_shadows.h), SkyRenderer (renderer_3d_sky.h) and WorldCuller (renderer_3d_worlds.h).

#include "spot/core/math/bounds.h"
#include "spot/core/math/color.h"
#include "spot/core/math/matrix.h"
#include "spot/core/math/vector.h"
#include "spot/graphics/buffer.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/render_context.h"
#include "spot/render/render_world.h"
#include "spot/render/renderer_3d.h"
#include "spot/render/shadow_views.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Spot::Detail {

// The material's textures, in the order the mesh shader binds them after the shadow maps.
using MaterialTextures = std::array<const Texture*, 5>;

// As the mesh shaders read each instance.
struct InstanceData {
    Matrix4 model;
    Matrix4 normalMatrix;
    std::array<std::uint32_t, 4> skin;
    Vector4 bounds;
};

struct DrawCommand {
    const Mesh* mesh;
    // Base color, metallic-roughness, normal, occlusion and emissive; null where the material has none.
    MaterialTextures textures;
    Color color;
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    Color emissive;
    AlphaMode alphaMode;
    float alphaCutoff;
    bool doubleSided;
    float worldTextureSize;
    TextureSpace textureSpace = TextureSpace::World;
    Matrix4 transform;
    // Around the mesh, in world space.
    BoundingSphere bounds;
    bool castsShadows;
    // Whether it is in the camera's view, which End() decides.
    bool visible = true;
    // Of its bounds' center along the camera's forward axis.
    float depth = 0.0f;
    // The same for commands whose material looks the same, textures and values alike, which End() gives.
    std::uint32_t look = 0;
    // The range of the mesh's indices it draws.
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    // Whether its transform turns the mesh inside out, which swaps the faces culled.
    bool mirrored = false;
    std::uint32_t skinOffset = 0;
    std::uint32_t skinCount = 0;
    // For a batch of a render world kept on the GPU: its instances, from the first on, which the shaders cull
    // themselves. The command's bounds are then around all of them.
    const Buffer* instanceBuffer = nullptr;
    std::uint32_t instanceFirst = 0;
    std::uint32_t instanceCount = 1;
    // The batch's instances as uploaded, which shadow views cull one by one.
    const InstanceData* instanceData = nullptr;
    // When the culling pass ran for the frame: the instances it kept, and the batch's indirect draw in arguments,
    // the batch-th.
    const Buffer* keptInstances = nullptr;
    const Buffer* arguments = nullptr;
    std::uint32_t batch = 0;
    bool nearestFiltering = false;
};

// The view, and its planes, which batches whose instances the shaders cull test them against.
struct FrameUniforms {
    Matrix4 viewProjection;
    std::array<Vector4, 6> cullPlanes;
    // Where the camera that picks levels of detail is, and how it sizes things: an instance's screen size is its
    // radius times w over its distance, or times -w alone when w is negative, for orthographic cameras.
    Vector4 lodView;
};

inline FrameUniforms MakeFrameUniforms(const Matrix4& viewProjection, const Camera3D& camera, bool ignoreNear = false) {
    return {viewProjection, Frustum(viewProjection, ignoreNear).GetPlanes(), GetLodView(camera)};
}

// These mirror the shaders' buffers.
struct BatchUniforms {
    std::array<std::uint32_t, 4> firstInstance;
};

struct FogUniforms {
    Color color;
    Vector4 params;
    Vector4 camera;
};

FogUniforms GetFogUniforms(const std::optional<FogData>& fog, Vector3 cameraPosition);

// The inverse transpose of the model matrix's rotation and scale, which keeps normals perpendicular under non-uniform
// scale. Its last column, which turns nothing as normals have no w, tells the shaders more: its last element is -1 for
// transforms that mirror, and its first 1 for meshes that project their textures from their own space.
Matrix4 GetNormalMatrix(const Matrix4& m, TextureSpace textureSpace = TextureSpace::World);

inline Vector4 ToVector4(Vector3 v, float w = 0.0f) {
    return {v.x, v.y, v.z, w};
}

// How much a light matters to what is near the camera, lowest first: directional lights reach everything.
float GetLightRank(const LightData& light, Vector3 cameraPosition);

// How a sky lights what is under it: the sun through the air, which dims and reddens it near the horizon and hides it
// below, the moon opposite it once it has set, and the light the rest of the sky scatters down.
struct SkyLighting {
    std::optional<LightData> sun;
    // Only while the moon is up and shown.
    std::optional<LightData> moon;
    // The moon's light before the air dims it, which the air scatters into a faint moonlit sky.
    Color moonlight;
    Color ambient;
    // The light of the sky alone, from above, whatever lights the scene and however strongly: what lights the shaded
    // sides of what is up in it, such as clouds.
    Color skylight;
    // What reaches open ground: the sun's and the moon's light, and the sky's.
    Color ground;
    // What lights a point in place of the sky it is covered from: the ground's light, reflected again by the underside
    // of what covers it.
    Color bounce;
};

SkyLighting GetSkyLighting(const SkyData& sky);

struct SelectedLights {
    Color ambient = Color::Black;
    // At most MaxLights, none of them ambient.
    std::vector<const LightData*> lights;
    // Per light: 0 or more when it casts shadows, as the shaders' lights hold it.
    std::array<float, Renderer3D::MaxLights> shadows{};
    std::array<float, Renderer3D::MaxLights> shadowTexelSizes{};
};

struct ShadowUniforms {
    std::array<Matrix4, MaxShadowCascades> cascadeViews;
    std::array<Matrix4, Renderer3D::MaxShadowTiles> tileViews;
    Vector4 cascadeEnds;
    Vector4 cascadeTexelSizes;
    // w is the number of cascades.
    Vector4 cameraForward;
    // The columns and rows of tiles in each shadow map, and the size of their texels in UVs.
    Vector4 cascadeLayout;
    Vector4 tileLayout;
    Matrix4 skyOcclusionView;
    // 1 when there is sky occlusion, the radius its lookups spread over, the depth a unit of height spans, and the size
    // of its texels, in world units.
    Vector4 skyOcclusionParams;
};

// The directions of each face of a cube texture, as TextureType::Cube lays them out: forward, right and up.
inline constexpr std::array<std::array<Vector3, 3>, 6> CubeFaces{{
    {{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}}},
    {{{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}}},
    {{{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}},
    {{{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}},
    {{{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}},
    {{{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}},
}};

// Uploads the elements, growing the buffer to the next power of two when they do not fit. Buffers are never empty,
// so they can be bound when there is nothing to put in them.
template <typename T>
bool UploadStorage(GraphicsDevice& device, std::unique_ptr<Buffer>& buffer, const std::vector<T>& elements) {
    const auto size = static_cast<std::uint32_t>(std::max<std::size_t>(elements.size(), 1) * sizeof(T));
    if (!buffer || buffer->GetSize() < size) {
        std::uint32_t capacity = 64 * sizeof(T);
        while (capacity < size) {
            capacity *= 2;
        }
        buffer = device.CreateBuffer({.usage = BufferUsage::Storage, .size = capacity});
        if (!buffer) {
            return false;
        }
    }
    // Discarded, so views drawn earlier in the frame keep what they were drawn with.
    if (!elements.empty()) {
        device.UploadToBuffer(*buffer, elements, 0, UploadMode::Discard);
    }
    return true;
}

// What the parts of Renderer3D share: the frame being drawn, from Begin() to End(), and the resources every part
// binds.
struct Renderer3DState {
    Renderer3DState(GraphicsDevice& device, const Renderer3DConfig& config);

    bool IsValid() const { return sampler && nearestSampler && clampSampler && whiteTexture; }

    // Gives the frame's custom surfaces the view, before its pass opens, and returns the view's index.
    std::uint32_t PrepareSurfaces(RenderContext& renderContext, SurfacePass kind, const Matrix4& view, bool ignoreNear);
    void DrawSurfaces(RenderPass& pass, SurfacePass kind, const Matrix4& view, bool ignoreNear,
                      std::uint32_t viewIndex);

    GraphicsDevice* device;
    Renderer3DConfig config;
    std::unique_ptr<Sampler> sampler;
    std::unique_ptr<Sampler> nearestSampler;
    // Linear, clamped to the edge, for images of the view and atlases.
    std::unique_ptr<Sampler> clampSampler;
    // Untextured materials sample this, so one shader serves both kinds.
    std::unique_ptr<Texture> whiteTexture;
    // The transforms of every command of the frame, which instanced draws read from the first of their batch on, and
    // the joints of its skinned meshes.
    std::unique_ptr<Buffer> instances;
    std::unique_ptr<Buffer> palettes;

    RenderContext* context = nullptr;
    RenderPassDesc target;
    Camera3D camera;
    Matrix4 viewProjection;
    Vector2 viewSize = {1.0f, 1.0f};
    float aspectRatio = 1.0f;
    std::optional<FogData> fog;
    std::vector<DrawCommand> commands;
    std::vector<Matrix4> paletteData;
    // The frame's custom surfaces, kept until the next Begin() for RenderCameraDepth().
    std::vector<CustomSurface*> surfaces;
    std::uint32_t surfaceViewIndex = 0;
    Renderer3DStats stats;
    // Counts End() calls, so worlds no longer drawn are let go.
    std::uint64_t endCount = 0;
};

} // namespace Spot::Detail
