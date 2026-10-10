# Rendering

Rendering has two layers. The **graphics API** (`GraphicsDevice`, `RenderContext`, `RenderPass`, buffers, textures,
shaders and pipelines) is a thin, backend-independent interface over SDL_GPU, whose backend lives in
`engine/graphics/sdl_gpu/` and never shows in public headers. The **renderers** (`Renderer2D`, `Renderer3D`,
`LineRenderer`, `SceneRenderer`) are built only on that public API, so games can do anything they do, or draw their
own way next to them.

## The graphics API

`Application` creates the `GraphicsDevice` for its window (`GraphicsConfig` turns graphics off for headless runs).
Each frame, `BeginFrame()` returns a `RenderContext` (null when nothing can be drawn, as while minimized), which opens
`RenderPass`es into the window or into render target textures; `EndFrame()` submits it.

- **Resources**: `CreateBuffer()`, `CreateTexture()`, `CreateSampler()`, `CreateShader()` and `CreatePipeline()`
  return null, after logging why, when the backend rejects them. Uploads (`UploadToBuffer()`, `UploadToTexture()`)
  can happen at any time and reach the GPU before the frame's draws; `UploadMode::Discard` suits data rewritten every
  frame. `DownloadTexture()` reads pixels back, slowly, for tools and tests.
- **Blending**: `None`, `Alpha` (straight alpha), `Additive`, and `Premultiplied` for colors already multiplied
  by their coverage.
- **Pipelines** are immutable: one per combination of shaders, vertex layout, primitive type, blending, culling,
  depth state, target format and sample count, created ahead of time.
- **Passes** clear or keep their targets, can have a depth target, and with several samples resolve into a single
  sample target (`keepSamples` lets a later pass draw more over the samples). They draw into one face and mip level of
  their target (`targetLayer`, `targetMipLevel`), and `RenderContext::GenerateMipmaps()` makes a render target's
  levels from its first.
- **Cube textures** (`TextureType::Cube`) are six square faces that shaders sample by direction (`TextureCube`), laid
  out as Direct3D, Vulkan and Metal agree (see `TextureType`). Uploads and downloads hold their faces one after the
  other, and downloads can read any mip level.
- **Colors** are linear. The window's format is sRGB, so shaders write linear colors and the window encodes them.
- **Formats**: 8-bit RGBA and BGRA, sRGB or not; `Rgba16Float` and `R11G11B10Float` for HDR render targets, which
  hold values above 1 (`SupportsRenderTarget()` tells whether the GPU draws into the packed one); and
  `Depth32Float`. `GetBytesPerPixel()` gives the size of a pixel as uploads and downloads hold it. Multisampled
  targets of any color format resolve into single-sample render targets, which later passes sample.

### Fullscreen passes

`FullscreenPass` (`engine/graphics/fullscreen_pass.h`) runs a fragment shader over every pixel of a pass's target,
as post-processing effects do: one triangle covering the target, built from the vertex index, with texture
coordinates from (0, 0) at its top-left corner. The fragment shader takes `SV_Position` then `float2 uv :
TEXCOORD0`; `Draw()` binds its pipeline and the input textures, and uniforms pushed before stay set.

```cpp
auto tonemap = Spot::FullscreenPass::Create(device, Shaders::TonemapFrag, {.targetFormat = device.GetWindowFormat()});
auto pass = context.BeginRenderPass();
pass.PushFragmentUniforms(0, exposure);
const std::array inputs{Spot::FullscreenInput{hdr.get(), sampler.get()}};
tonemap->Draw(pass, inputs);
```

### Shaders

Shaders are HLSL files named `*.vert.hlsl` or `*.frag.hlsl`. `spot_add_shaders(<target> ...)`
(`cmake/spot_shaders.cmake`) compiles them to SPIR-V, MSL and, on Windows, DXIL, and generates a header per shader
with a ready-to-use `ShaderDesc` (the engine's are in `Spot::Detail::Shaders`). Conventions follow SDL_GPU:
vertex uniforms at `register(b0, space1)`, fragment textures and samplers at `register(t0, space2)` and
`register(s0, space2)`, and vertex inputs as `TEXCOORD{N}` for attribute `N`.

## Renderers

| Renderer | Draws |
|---|---|
| `Renderer2D` | Colored and textured quads and text, batched, in the order given, with alpha blending, inside a pass the caller opens; `DrawScene()` draws a scene's `SpriteRenderer`s |
| `Renderer3D` | Lit, textured, shadowed meshes and models with a sky; it opens the pass described to `Begin()` in `End()`, after drawing shadow maps; `DrawScene()` draws a scene's `MeshRenderer`s, `Light`s and first `Sky` |
| `LineRenderer` | One-pixel lines in 3D, depth-tested or over everything, inside a pass the caller opens; `DrawDebugLines()` draws the `DebugDraw` queue |
| `SceneRenderer` | Whole scenes as games show them, through their main camera |

### SceneRenderer

`SceneRenderer::Render()` draws scenes over each other into the window or a texture: their meshes through the first
main camera among them (`FindMainCamera()`), then the shapes queued with `DebugDraw` (see
[Diagnostics](diagnostics.md)), then their sprites, all as light into an HDR image (`Rgba16Float`, multisampled where
the GPU allows), which a tonemapper turns into the target's colors; then their interfaces over it, as they are
(`Scene::DrawUi()`, which calls each active component's `OnDrawUi()` with a renderer in pixels from the view's
bottom-left corner). The application draws its scenes with one unless `ApplicationConfig::renderScenes` is off, and
the editor's views draw the same way.

### Render hooks

Games and systems draw passes of their own at fixed points of a frame (`RenderStage`): `BeforeOpaque` (into the
cleared HDR image; a sky draws over it), `AfterOpaque` (with the surfaces' depth; where `DebugDraw`'s shapes are
drawn), `AfterTransparent`, `BeforePostProcessing`, `AfterPostProcessing` (into the target, in its colors, before the
interfaces) and `Overlay` (over everything). A hook gets a `RenderStageContext`: the camera and view-projection, a
`RenderPassDesc` into the stage's targets that keeps what they hold (depth included before post-processing), the
format and sample count its pipelines must match, the depth target, and the HDR image so far.

```cpp
void OnEnable() override {
    m_hook = GetScene().AddRenderHook(Spot::RenderStage::AfterOpaque, [this](const Spot::RenderStageContext& stage) {
        auto pass = stage.context.BeginRenderPass(stage.pass);
        // ... draw with pipelines made for stage.colorFormat and stage.sampleCount ...
    });
}
void OnDisable() override { GetScene().RemoveRenderHook(m_hook); }
```

`Scene::AddRenderHook()` hooks run wherever the scene is drawn, the editor's views included; `SceneRenderer::AddHook()`
hooks run first, for every scene that renderer draws. A hook that throws is logged and removed.

### HDR and tonemapping

Light in a scene is not limited to what a display shows: a lamp, the sun or a glowing surface can be many times
brighter than white. `Tonemapper` brings an HDR image within the display's range with `TonemapSettings`: an operator
(`Aces`, contrasty with saturated colors, the default; `AgX`, gentler, turning bright saturated light toward white;
or `Linear`, which clips, for views that must show colors as they are), an exposure that multiplies the light first,
and dithering that hides banding. `SceneRendererConfig::tonemapping` and `SceneRenderer::SetTonemapping()` choose
them. `Renderer3DConfig::hdr` makes Renderer3D draw the sky's light as it is, for the tonemapper, rather than mapping
it itself as it does into displayable targets.

### Bloom

`Bloom` spreads light brighter than the display shows, as a lens does: it halves the bright parts of the HDR image
again and again (a 13-tap filter that keeps small bright details from flickering), then spreads them back up, each
level blending a wider glow over the one above it. `BloomSettings` has the threshold below which light does not glow
(fading in over half of it), the scatter, from a tight glow to one over the whole image, and the intensity the
tonemapper adds the glow with; an intensity of 0 turns it off. `SceneRendererConfig::bloom`, `SceneRenderer::SetBloom()`
and `ViewportOptions::bloom` choose them.

### Grading

`GradingSettings` changes the image's colors in the tonemapper's pass: on light, before the curve, a white balance
(`temperature` from cooler to warmer, `tint` from greener to more magenta, keeping the brightness), `contrast` around
middle grey in stops, and `saturation`; on the displayed colors, after it, a `vignette` that darkens the corners from
`vignetteStart`, and a LUT. LUTs are color cubes laid out as strips of their blue slices (`size * size` by `size`
pixels, such as 256 by 16) that map displayed (sRGB) colors: grade `MakeNeutralLut()` in an image editor and load it
as an `Rgba8` texture, which keeps its values as they are. `SceneRendererConfig::grading`,
`SceneRenderer::SetGrading()` and `ViewportOptions::grading` choose them.

### The look and the rendering settings

The look of a game comes from the first active `PostProcessing` component among the scenes drawn: its tonemapper
and exposure, bloom and grading, LUT included (a texture asset imported with `srgb` off in its import settings, so
its values are read as they are). Without one, the rendering settings section of the project (`RenderingSettings`,
"rendering") gives the defaults, along with the quality the application draws at: FXAA, MSAA samples, and the
sizes and distance of shadows. The editor's views use the same, and the Project Settings panel and the inspector
edit both.

### Antialiasing

Multisampling smooths the edges of what is drawn into the HDR image, where the GPU allows. `Fxaa` smooths what it
does not reach, such as thin highlights and the edges inside textures, after tonemapping, for little cost and a
slightly softer image: `SceneRendererConfig::fxaa`, `SceneRenderer::SetFxaaEnabled()` and `ViewportOptions::fxaa`
turn it on, next to multisampling or instead of it. The interfaces are drawn after it, so text stays sharp.

## Scene components

- `Camera`: where a scene is seen from, looking down its entity's -Z; perspective or orthographic, with a
  background color.
- `MeshRenderer`: a mesh with a material at its entity's world transform. Materials (`Material`) are
  metallic-roughness PBR, as glTF defines it, so models look as their authors made them: a base color, `metallic`
  (0 for dielectrics, 1 for metals) and `roughness` (0 for a mirror, 1, the default, for a matte surface), each with
  an optional texture that multiplies it (roughness in green and metallic in blue of one texture, as glTF packs
  them); a tangent-space normal map with its `normalScale`; an occlusion map, which darkens ambient light by
  `occlusionStrength`; and an `emissive` color and texture the surface gives off itself, which glow through bloom
  above 1. Dielectrics scatter their color and reflect about 4% of the light untinted; metals scatter nothing and
  reflect in their color. Highlights are GGX microfacets, as wide as the roughness says. Materials can also be
  double-sided, and project their textures from world space (`worldTextureSize`), which keeps the mesh's normals.
  Normal, metallic-roughness and occlusion textures hold data, so they are imported with `srgb` off; models do this
  for the images they use only that way. A material's `alphaMode` says what its alpha does: `Opaque` ignores it,
  `Cutout` drops pixels below `alphaCutoff` (in shadows too) for leaves and grates, and `Transparent` lets what is
  behind show through as much as alpha says while reflecting at full strength, as glass does. Transparent surfaces
  are drawn after the others, sorted from the farthest to the nearest by the center of their bounds, without writing
  depth, and cast no shadows; surfaces that cross each other can still sort wrongly. glTF's `alphaMode` (`OPAQUE`,
  `MASK`, `BLEND`) and `alphaCutoff` are imported as these.
- Meshes (`MeshData`) are vertices with a position, a normal, texture coordinates and a tangent, the direction the
  texture's u grows along the surface, with in `w` the side its v grows on (the bitangent is
  `cross(normal, tangent.xyz) * w`), in which normal maps are read. Models use the tangents their glTF files give,
  and `GenerateTangents()` makes the others with MikkTSpace, as normal maps are baked, splitting vertices that need
  two; the `MeshData` builders call it, and so must code that builds meshes itself and uses normal maps.
- `SpriteRenderer`: a texture or color on a rectangle in its entity's XY plane, for 2D.
- Lights: `AmbientLight`, `DirectionalLight`, `PointLight` and `SpotLight`. Up to `Renderer3D::MaxLights` light each
  frame. Intensities are illuminance, in units where 1 makes a white matte surface facing the light show white
  before exposure: ambient and directional lights give theirs to every surface, and point and spot lights give
  theirs at one unit of distance and fall off with the square of the distance (so their intensity is a luminous
  intensity, like candelas). Their falloff is windowed to reach nothing at their range, which changes little up to
  half of it; a bulb lighting a room a few units across is about 10, the point and spot lights' default. Lights can cast shadows: the first directional light through cascades, and point and spot lights through
  tiles of a shared shadow atlas.
- `Sky`: an atmosphere lit by the scene's brightest directional light, which turns from day to sunset to night as
  that light turns, and lights the scene with the sun through the air and with its own light from all around. When
  the sky changes, `Renderer3D` captures that light into a cube map (without the sun's disc, which the directional
  light already is), blurs it into the mip levels of a second cube for each roughness (GGX, as split-sum image-based
  lighting does) and projects its irradiance onto nine spherical harmonics. Surfaces then scatter the irradiance
  along their normal and reflect the blurred sky along their mirror direction, so the ground's color lights
  undersides and metals mirror the sky. `GetStats().skyLightingUpdates` tells when a capture happened; a sky that
  turns all the time, as the sandbox's does, is captured every frame, which costs a few small passes.
- `Fog`: exponential height fog from its entity's height, thick below it and thinning above it as `heightFalloff`
  says, clear up to `start`; `density` is how much light it takes per unit of distance. Surfaces fade into its color
  (which may be above 1, as lit fog is) before tonemapping, and so does the sky toward the horizon, so the two meet
  there in the same color. `Renderer3D::DrawFog()` takes a `FogData` directly.

## Golden-image tests

`tests/golden_tests.cpp` renders reference scenes as games show them, through `SceneRenderer` (without
multisampling, dithering or bloom, which vary the most between GPUs), and compares them with the images stored in
`tests/golden/`: PBR spheres from dielectric to metal and smooth to rough, cutout and transparent surfaces with
shadows, a normal-mapped wall, and the glTF sample models DamagedHelmet and AlphaBlendModeTest, which the build
downloads into the build folder (pinned and checked by hash; `SPOT_DOWNLOAD_SAMPLE_MODELS=OFF` skips them). A render
may differ from its image by a few steps here and there, as GPUs round differently, but not by more than 12 steps
in over 0.3% of its pixels, nor by a mean of more than a step. A failure leaves the render and an image of its
differences in the build's `tests/golden_output/`. When a change to how things look is meant, run the tests with
`SPOT_UPDATE_GOLDEN=1` to store the new images, look at them, and commit them with the change.

## Not there yet

Culling and instancing come in the roadmap's rendering milestone M7.
