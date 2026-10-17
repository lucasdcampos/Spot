# Rendering

`MeshRenderer` saves materials with asset IDs as references. A material created or painted in code has no
material asset ID and is saved inline, including PBR values and texture asset references. Scene copies and
checkpoints therefore preserve paint and emissive lamp colors. Code-created meshes still need an asset ID to
survive serialization.

For GPU work without a desktop window, use `GraphicsDevice::CreateOffscreen()` and
`BeginOffscreenFrame(colorTarget)`. Default render passes draw into that target until `EndFrame()` submits the
commands. The target must be a single-sample 2D color render target and remain alive for the frame; read it back
after submission. `GetWindowFormat()` supplies the offscreen device's default color format (RGBA, sRGB when
`linearWindow` is enabled). VSync has no effect. See [developer-tool testing](developer_tools.md#testing-without-windows).

Rendering has two layers. The **graphics API** (`GraphicsDevice`, `RenderContext`, `RenderPass`, buffers, textures,
shaders and pipelines) is a thin, backend-independent interface over SDL_GPU, whose backend lives in
`engine/graphics/sdl_gpu/` and never shows in public headers. The **renderers** (`Renderer2D`, `Renderer3D`,
`LineRenderer`, `SceneRenderer`) are built only on that public API, so games can do anything they do, or draw their
own way next to them.

## The graphics API

`Application` creates the `GraphicsDevice` for its window (`GraphicsConfig` turns graphics off for headless runs).
Each frame, `BeginFrame()` returns a `RenderContext` (null when nothing can be drawn, as while minimized), which opens
`RenderPass`es into the window or into render target textures; `EndFrame()` submits it.

- **Buffers** hold vertices, indices, or data shaders read as a `StructuredBuffer` (`BufferUsage::Storage`), which
  `RenderPass::BindVertexStorageBuffer()` and `BindFragmentStorageBuffer()` bind.
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

### Backends

The graphics API is the engine's own; backends implement it behind `engine/graphics/graphics_backend.h`, where each
public object holds an implementation the backend derives from. Several backends are built into one program, and
the device picks one when it is made, from `GraphicsConfig::backend`, which the environment variable
`SPOT_GRAPHICS_BACKEND` (`sdl_gpu` or `vulkan`) overrides:

- **SDL_GPU** (`graphics/sdl_gpu/`), over Direct3D 12, Vulkan or Metal, on every platform. It is the default for now.
- **Vulkan through NVRHI** (`graphics/nvrhi/`), on Windows and Linux, which the engine is moving to: NVRHI tracks
  resource states and places barriers, and opens compute, bindless resources, indirect draws and GPU timers. The
  backend makes the Vulkan 1.3 instance, device and swapchain NVRHI runs on (`nvrhi_vulkan.cpp`), on the best GPU
  that runs it, with Vulkan's validation layer in debug builds when it is installed. CMake fetches NVRHI and Vulkan's
  headers, so building needs no SDK; running needs only the GPU's driver. It clears, resolves, uploads and downloads
  textures so far; shaders, pipelines, buffers and draws come next, and until then the device logs what it cannot do.

macOS keeps SDL_GPU, whose Metal NVRHI lacks.

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
| `Renderer2D` | Colored and textured quads and text, batched, in the order given, with alpha blending, inside a pass the caller opens; `SetTransform()` places what follows, as world text and sprites at a depth need |
| `Renderer3D` | Lit, textured, shadowed meshes and models with a sky; it opens the pass described to `Begin()` in `End()`, after drawing shadow maps |
| `LineRenderer` | One-pixel lines in 3D, depth-tested or over everything, inside a pass the caller opens; `DrawDebugLines()` draws the `DebugDraw` queue |
| `SceneRenderer` | Whole scenes as games show them, through their main camera (`engine/scene/rendering/`) |

The renderers know nothing of scenes. `engine/scene/rendering/scene_drawing.h` draws a scene's components with them:
`DrawScene(Renderer3D&, Scene&)` its `MeshRenderer`s, `SkinnedMeshRenderer`s, `Light`s and first `Sky` and `Fog`,
`DrawScene(Renderer2D&, Scene&)` its sprites and tilemaps, `DrawWorldText()` its `TextRenderer`s and
`DrawScene(ParticleRenderer&, Scene&)` its particle systems. It uses only the renderers' public API, so games that
keep their objects some other way draw them the same way.

### Models and submeshes

`ModelData` reads a glTF scene into nodes with names, local matrices, parents and mesh indices. Parents precede
their children; nodes without meshes remain in the hierarchy. Mesh vertices stay in mesh space, and nodes sharing
a glTF mesh share its buffers. Each triangle primitive becomes a `SubMesh` index range with its own material.
`MeshData::AppendSubmesh()` combines geometry into these ranges; meshes without explicit ranges have one range
covering every index.

`Model::GetNodes()` exposes the loaded hierarchy, and `GetNodeTransform()` gives a node's matrix in model space.
`GetParts()` exposes its drawable submeshes with their accumulated matrices. `Renderer3D::DrawModel()` multiplies
those matrices by the caller's transform. `DrawMesh()` can select a submesh, or draw the whole mesh with
`Mesh::AllSubmeshes` (the default). An invalid submesh is logged and skipped. Batching and shadow passes use the
same index ranges. Negative scales reverse culling and tangent handedness, so mirrored nodes retain their surfaces
and normal maps.

### Culling

`Renderer3D` draws only what can be seen. Each shadow pass leaves out the meshes outside its light's view, and
`End()` leaves out of the view's pass the meshes whose bounds are outside the camera's frustum; `GetStats()` and the
profiler count both (`visibleMeshes` and `culledMeshes`). A camera also sees only the layers in its `cullingMask`
(`Camera3D::cullingMask`, set on the `Camera` component, which the inspector shows as layers to tick):
`DrawScene(Renderer3D&, Scene&)` skips the meshes of entities on the others, in its shadows too.

### Shadows

The first directional light that casts shadows draws them into cascades: slices of the view up to
`shadowDistance`, split more finely near the camera, each seen by the light through an orthographic view around the
smallest sphere holding its slice. The sphere keeps its size as the camera turns, and its view moves in whole texels,
so shadows do not shimmer as the camera moves; each cascade blends into the next over its last tenth, and the last
fades out. Shadow pipelines clamp depth rather than clip it, so what lies between the light and a cascade's slice,
such as a roof above the camera, still casts its shadow. Point and spot lights draw theirs into tiles of a shared
atlas, six per point light. Each pass draws only the casters its view sees. Surfaces look shadows up through 16
comparisons on a small Poisson disk, each blending four texels, so edges fade over about four texels, the same way at
every pixel, so they do not flicker.

### Clustered lights

Up to `Renderer3D::MaxLights` (256) lights light each frame, ambient ones aside, without each pixel going through all
of them. The view is cut into clusters, 16 by 9 tiles of the screen by 24 slices of depth, even in the logarithm of
depth so near slices are thin; each frame the CPU lists, for each cluster, the point and spot lights whose spheres
may reach it (`Detail::BuildLightClusters()`), and the lights and lists go to the GPU in storage buffers. Each pixel
finds its cluster from its position and depth and goes through the directional lights, which reach everything, and
its cluster's lights. Shadows keep their own budget: the first directional light's cascades, and
`Renderer3D::MaxShadowTiles` tiles for point and spot lights, given to those nearest the camera.

### Instancing

`Renderer3D` draws meshes that look the same together. It sorts small keys, then moves each command once. It sorts the frame's opaque meshes by pipeline, then material,
then mesh, then from the nearest to the farthest, so state changes as rarely as it can (`GetStats()` counts
`pipelineChanges` and `materialChanges`) and what is in front hides what is behind before it is shaded; meshes with the
same mesh, material values and pipeline thus sit side by side. It then puts every mesh's transforms in one storage
buffer, and draws each run of them in view as instances of one draw, in the view's pass and in each shadow pass. Nothing
needs to be marked: a level of the same crates is a handful of draws (`GetStats().drawCalls` counts draws, not meshes).
Materials batch by their values, so copies of a material batch as well as the same asset.

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
- `TextRenderer`: SDF text in its entity XY plane, optionally camera-facing; see [Runtime UI and text](runtime_ui.md).
- `SpriteRenderer`: a texture or color on a rectangle in its entity's XY plane, for 2D.
- Lights: `AmbientLight`, `DirectionalLight`, `PointLight` and `SpotLight`. Up to `Renderer3D::MaxLights` (see Clustered
  lights) light each frame. Intensities are illuminance, in units where 1 makes a white matte surface facing the light
  show white before exposure: ambient and directional lights give theirs to every surface, and point and spot lights
  give theirs at one unit of distance and fall off with the square of the distance (so their intensity is a luminous
  intensity, like candelas). Their falloff is windowed to reach nothing at their range, which changes little up to half
  of it; a bulb lighting a room a few units across is about 10, the point and spot lights' default. Lights can cast
  shadows: the first directional light through cascades, and point and spot lights through tiles of a shared shadow
  atlas.
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

## The rendering benchmark

`spot_benchmark` (`tests/benchmark/`) renders a scene of 10,000 meshes and 200 lights under the sky, its sun and a few
shadowed point and spot lights, through a camera circling it, as games draw scenes, without vsync. After warming up it
times 600 frames and prints their mean, median, 95th percentile and worst times, the last frame's counts of meshes,
draw calls and lights, and the costliest profiler scopes. Run its Release build when rendering changes, and compare
with the times recorded in the M7 notes of `ROADMAP.md`:

```sh
cmake --build --preset msvc-release --target spot_benchmark
./build/msvc/tests/Release/spot_benchmark      # --frames <count> --warmup <count>
```

With `--scene <file> --assets <imported folder>`, it times a project's scene instead, its cameras off, with
`--lamps <count>` lamps (48 by default) spread over it, a quarter of them spots and an eighth casting shadows; the
sandbox's playground, from the assets its build imported:

```sh
./build/msvc/tests/Release/spot_benchmark --scene sandbox/assets/scenes/playground.scene --assets build/msvc/sandbox/Debug/imported
```

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

## Particles

SceneRenderer and editor viewports draw instanced HDR particles before post processing.
See [Particles](particles.md) for the code-only renderer, sorting, flipbooks and readable camera depth.
`Renderer3D::RenderCameraDepth()` exposes an optional single-sample opaque/cutout depth pass from the
last frame's submissions, useful when a consumer needs readable depth from a multisampled view.

Games can apply per-user quality with `Application::SetRenderingSettings()` without resetting input bindings or changing project defaults. The renderer is rebuilt at the next scene render; `GetRenderingSettings()` and `GetRenderingRevision()` let custom hosts follow the same runtime configuration. The editor game view uses the public SceneRenderer path and follows these changes too.

Skinned meshes use `SkinnedMeshRenderer` or `Renderer3D::DrawSkinnedMesh()` with a second
vertex stream and per-instance joint palettes. Color, shadows, readable camera depth and
editor picking use the same deformation. Bounds padding covers animation beyond the bind mesh;
see [animation](animation.md).
