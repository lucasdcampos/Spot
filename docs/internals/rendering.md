# Rendering

Materials can opt into `nearestFiltering` to keep pixel art texels sharp while other materials inherit the
renderer's filtering. This sampler selection is part of material batching and applies to lit, cutout shadow and
camera-depth passes. It is saved for inline and imported materials; existing materials default to the renderer's
filter. Texture mipmaps still limit distant shimmer.

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
shaders and pipelines, in `engine/spot/graphics/`) is the engine's own, independent of the backends that run it
(Vulkan through NVRHI, or SDL_GPU; see [Backends](#backends)), which never show in public headers. The **renderers**
(`Renderer2D`, `Renderer3D`, `LineRenderer`, with meshes, materials, models, fonts, cameras, lights, skies, shadows and
post-processing, in `engine/spot/render/`) are built only on that public API, so games can do anything they do, or
draw their own way next to them. `SceneRenderer` draws scenes with them.

## The graphics API

`Application` creates the `GraphicsDevice` for its window (`GraphicsConfig` turns graphics off for headless runs).
Each frame, `BeginFrame()` returns a `RenderContext` (null when nothing can be drawn, as while minimized), which opens
`RenderPass`es into the window or into render target textures; `EndFrame()` submits it.

- **Buffers** hold vertices, indices, or data shaders read as a `StructuredBuffer` (`BufferUsage::Storage`), which
  `RenderPass::BindVertexStorageBuffer()` and `BindFragmentStorageBuffer()` bind.
- **Resources**: `CreateBuffer()`, `CreateTexture()`, `CreateSampler()`, `CreateShader()` and `CreatePipeline()`
  return null, after logging why, when the backend rejects them. Uploads (`UploadToBuffer()`, `UploadToTexture()`)
  can happen at any time and reach the GPU before the frame's draws; `UploadMode::Discard` suits data rewritten every
  frame. Backends reuse their upload memory (one command list on Vulkan, one growing transfer buffer on SDL_GPU),
  so many small uploads a frame cost no allocations; on Vulkan they are also submitted together, before the next
  frame or anything that reads back, so loading hundreds of textures is not hundreds of submissions. `DownloadTexture()` reads pixels back, slowly, for tools and tests.
- **Blending**: `None`, `Alpha` (straight alpha), `Additive`, and `Premultiplied` for colors already multiplied
  by their coverage.
- **Pipelines** are immutable: one per combination of shaders, vertex layout, primitive type, blending, culling,
  depth state, target format and sample count, created ahead of time.
- **Passes** clear or keep their targets, can have a depth target, and with several samples resolve into a single
  sample target (`keepSamples` lets a later pass draw more over the samples). They draw into one face and mip level of
  their target (`targetLayer`, `targetMipLevel`), and `RenderContext::GenerateMipmaps()` makes a render target's
  levels from its first. A pass draws into several color targets at once (`additionalTargets`, up to
  `GraphicsCapabilities::maxColorTargets`), as G-buffers need: the fragment shader writes `SV_Target1` and after, and
  its pipelines list their formats in `PipelineDesc::additionalTargetFormats`.
- **Cube textures** (`TextureType::Cube`) are six square faces that shaders sample by direction (`TextureCube`), laid
  out as Direct3D, Vulkan and Metal agree (see `TextureType`). Uploads and downloads hold their faces one after the
  other, and downloads can read any mip level.
- **Colors** are linear. The window's format is sRGB, so shaders write linear colors and the window encodes them.
- **Formats**: 8-bit RGBA and BGRA, sRGB or not; `Rgba16Float` and `R11G11B10Float` for HDR render targets, which
  hold values above 1 (`SupportsRenderTarget()` tells whether the GPU draws into the packed one); formats of one, two
  or four channels for data (`R8`, `Rg8`, `R16Float`, `Rg16Float`, `R32Float`, `Rg32Float`, `Rgba32Float`,
  `R32Uint`, `Rgb10A2`), as G-buffers and compute passes use; and `Depth32Float`. `GetTextureDataSize()` gives the
  bytes of an image as uploads and downloads hold it. Multisampled targets of any color format resolve into
  single-sample render targets, which later passes sample.
- **Compressed textures** (`Bc1` to `Bc7`, where `GraphicsCapabilities::compressedTextures` says so) store 4x4 pixels
  in 8 or 16 bytes, which the GPU decodes as it samples them. They are never drawn into, so their mipmaps are made
  beforehand, by the tools, and uploaded level by level with `UploadToTextureLevel()`.
- **Capabilities**: `GetCapabilities()` tells what the device does beyond what every device does (compressed
  textures, compute, indirect draws, several color targets, GPU timers, bindless textures); what a device lacks it
  refuses, after logging why, so code checks first and takes another way.

### Compute and indirect draws

Where `GraphicsCapabilities::compute` says so (every backend but SDL_GPU's Metal driver, whose resource order the
shader build does not follow yet), `RenderContext::BeginComputePass()` opens a pass that runs compute shaders
(`*.comp.hlsl`, made into a `ComputePipeline`). A pass names what it writes when it begins
(`ComputePassDesc::textures`, mip levels of textures created with `TextureDesc::storage`, and `buffers`), since
SDL_GPU and Vulkan place their barriers there; shaders read sampled textures and storage buffers bound during the
pass, and uniforms. Registers follow SDL_GPU: what the shader reads in `space0`, what it writes at `u#` of `space1`,
uniforms in `space2`. glslang needs the format of the storage textures written, as `[[spv::format_rgba8]]`.

`BufferUsage::Indirect` buffers hold the arguments of draws and dispatches (`DrawIndirectArguments`,
`DrawIndexedIndirectArguments`, `DispatchIndirectArguments`), which compute passes may write, so the GPU sizes the work
of the next passes: `ComputePass::DispatchIndirect()`, and `RenderPass::DrawIndirect()` and `DrawIndexedIndirect()`,
which make up to `maxIndirectDrawCount` draws in one call. This is what GPU-driven rendering (culling on the GPU,
then drawing what survived) is built on.

### Texture tables

Where `GraphicsCapabilities::bindlessTextures` says so (Vulkan, through descriptor indexing), a `TextureTable` holds up
to 65536 textures that shaders index freely: a pass binds one table (`RenderPass::BindTextureTable()`), and draws
pick their textures by numbers their data holds, so they need no texture bound of their own and differ only in data,
as GPU-driven rendering needs. Pipelines that read it are made with `PipelineDesc::textureTable`, and their fragment
shader declares `Texture2D<float4> textures[] : register(t0, space4)`; the table keeps its textures alive, and its
entries can change at any time (`SetTextureTableEntry()`).

### GPU timers

Where `GraphicsCapabilities::timestamps` says so (Vulkan; SDL_GPU has none), `RenderContext::BeginGpuTimer()` and
`EndGpuTimer()` measure the GPU time of what is recorded between them, between passes, and nest.
`GraphicsDevice::GetGpuTimings()` reads them a frame or two later, once the GPU is done with their frame, as the
frame waits for its slot of frames in flight anyway. `Application` times its whole frame as "Frame" and adds every
timing to the profiler as a counter, "GPU Frame (ms)", which the editor's profiler shows with the others.

### Backends

The graphics API is the engine's own; backends implement it behind `engine/spot/graphics/graphics_backend.h`, where each
public object holds an implementation the backend derives from. Several backends are built into one program, and
the device picks one when it is made, from `GraphicsConfig::backend`. With `Auto`, the environment variable
`SPOT_GRAPHICS_BACKEND` (`sdl_gpu` or `vulkan`) chooses, as the tests do to run on each; otherwise the device takes
Vulkan where it can and SDL_GPU everywhere else, falling back to SDL_GPU when Vulkan fails to start.

- **Vulkan through NVRHI** (`graphics/nvrhi/`), on Windows and Linux, the default where the GPU runs Vulkan 1.3:
  NVRHI tracks resource states and places barriers, and opens compute, bindless resources, indirect draws and GPU
  timers. The backend makes the Vulkan instance, device and swapchain NVRHI runs on (`nvrhi_vulkan.cpp`), on the best
  GPU that runs it, with Vulkan's validation layer in debug builds when it is installed. Devices without a window load
  the Vulkan loader themselves, so they run under any SDL video driver. CMake fetches NVRHI and Vulkan's headers, so
  building needs no SDK; running needs only the GPU's driver. Vulkan has no mipmap generation, so the backend draws
  each level from the one above (`mip_downsample.frag.hlsl`).
- **SDL_GPU** (`graphics/sdl_gpu/`), over Direct3D 12, Vulkan or Metal, on every platform: macOS, whose Metal NVRHI
  lacks, and machines without Vulkan 1.3.

#### The contract

What a backend must do is written down in two parts, and `tests/rhi_tests.cpp` checks both on every backend built
and supported on the machine, each on an offscreen device.

- **The baseline**, which every device does and code never checks for: shaders and pipelines from `ShaderDesc`,
  vertex, index, uniform and storage buffers, indexed and instanced draws, every blend mode, back-face culling, depth
  targets with every comparison and bias, viewports and scissors, color render targets of every uncompressed format
  but those `SupportsRenderTarget()` names, 2D and cube textures with their mip levels, generated mipmaps, 4x
  multisampled RGBA8 targets resolved into textures or the window, uploads at any time and texture readbacks. Its
  tests are tagged `[baseline]`.
- **The capabilities** (`GraphicsCapabilities`), which a device may lack: compressed textures, compute, indirect
  draws, several color targets, GPU timers and texture tables. A backend reports only what it does, and refuses the
  rest after logging why; each capability's test runs where the device reports it and warns where it does not.

The engine's renderers use only the baseline, or take a fallback when a capability is off, so every game runs on
every backend. `GraphicsConfig::baselineOnly` makes a device report no capability, whatever its GPU does: GPU
tests make every engine renderer on such a device, and golden-image tests render a scene with and without it to see
that the fallbacks look the same. A new backend implements `graphics_backend.h` and passes `rhi_tests.cpp`; a new capability gets a
field, a refusal where it is off and a test.

Renderer and golden-image tests run on each platform's default backend (`ctest --preset msvc-debug-gpu`). When a
backend or a shader changes, the whole GPU suite runs on each backend: `msvc-debug-gpu-sdl-gpu` and
`msvc-debug-gpu-vulkan` (and their Release and Ninja variants) set `SPOT_GRAPHICS_BACKEND` for it.

### Fullscreen passes

`FullscreenPass` (`engine/spot/render/fullscreen_pass.h`) runs a fragment shader over every pixel of a pass's target,
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

### Render graphs

`RenderGraph` (`engine/spot/graphics/render_graph.h`) describes a frame as passes that declare the textures they read
and write, then runs them in the order they were added. Passes create the textures that live for the frame only
(`RenderGraphBuilder::Create()`), and import the others, the frame target included (`Import(name, nullptr)`).
Before running, the graph leaves out passes whose results nothing uses (writing an imported texture, or
`KeepAlways()`, is what keeps a pass), and backs transient textures with textures it keeps from frame to frame:
textures of the same description that are not in use at the same time share one, so a frame of many effects needs
few targets, and none are created once sizes settle. Backends order the GPU's work on textures themselves, so the
graph has no barriers to place. Built on the public API alone, it is how a game composes passes of its own too.

```cpp
Spot::RenderGraph graph; // kept across frames, so its textures are
const auto target = graph.Import("Window", nullptr);
Spot::RenderGraphTexture scene;
graph.AddPass("Scene", [&](Spot::RenderGraphBuilder& pass) {
    scene = pass.Create("Scene", {.width = w, .height = h, .format = Spot::TextureFormat::Rgba16Float,
                                  .renderTarget = true});
    pass.Write(scene);
}, [&](Spot::RenderGraphResources& resources) { /* draw into resources.GetTexture(scene) */ });
graph.AddPass("Tonemap", [&](Spot::RenderGraphBuilder& pass) { pass.Read(scene); pass.Write(target); },
              [&](Spot::RenderGraphResources& resources) { /* draw into the window */ });
graph.Execute(device, context);
```

### Shaders

Shaders are HLSL files named `*.vert.hlsl` or `*.frag.hlsl`. `spot_add_shaders(<target> ...)`
(`cmake/spot_shaders.cmake`) compiles them to SPIR-V, MSL and, on Windows, DXIL, and generates a header per shader
with a ready-to-use `ShaderDesc` (the engine's are in `Spot::Detail::Shaders`), holding a second SPIR-V for the
Vulkan backend, whose textures, samplers and buffers are numbered as NVRHI binds them. Conventions follow SDL_GPU:
vertex uniforms at `register(b0, space1)`, fragment textures and samplers at `register(t0, space2)` and
`register(s0, space2)`, and vertex inputs as `TEXCOORD{N}` for attribute `N`.

Shaders `#include` headers from their own folder, from the engine's shader headers (`engine/spot/render/shaders/include/`)
and from the folders given as `INCLUDE_DIRECTORIES`; a change to any `.hlsl` file there compiles them again. The engine
builds the shader tools once, and its CMake package hands games `spot_add_shaders()` with them
(`cmake/spot_shader_function.cmake`), so a game compiles shaders of its own in its `CMakeLists.txt`, after
`spot_add_game()`:

```cmake
spot_add_shaders(game_code terrain.vert.hlsl terrain.frag.hlsl)
```

## Renderers

| Renderer | Draws |
|---|---|
| `Renderer2D` | Colored and textured quads and text, batched, in the order given, with alpha blending, inside a pass the caller opens; `SetTransform()` places what follows, as world text and sprites at a depth need |
| `Renderer3D` | Lit, textured, shadowed meshes and models with a sky; it opens the pass described to `Begin()` in `End()`, after drawing shadow maps |
| `LineRenderer` | One-pixel lines in 3D, depth-tested or over everything, inside a pass the caller opens; it draws the lines it is given, and `SceneRenderer` gives it the `DebugDraw` queue |
| `SceneRenderer` | Whole scenes as games show them, through their main camera (`engine/spot/scene/rendering/`) |

The renderers know nothing of scenes. `engine/spot/scene/rendering/scene_drawing.h` draws a scene's components with them:
`DrawScene(Renderer3D&, Scene&)` its `MeshRenderer`s, `SkinnedMeshRenderer`s, `Light`s and first `Sky` and `Fog`,
`DrawScene(Renderer2D&, Scene&)` its sprites and tilemaps, `DrawWorldText()` its `TextRenderer`s and
`DrawScene(ParticleRenderer&, Scene&)` its particle systems. It uses only the renderers' public API, so games that
keep their objects some other way draw them the same way.

### Inside Renderer3D

`renderer_3d.h` is small: the public API over an `Impl`, so the parts it draws with change without touching code that
uses it. The `Impl` (`renderer_3d.cpp`) selects the frame's lights, uploads them and runs `End()`; the parts share a
`Renderer3DState` (`renderer_3d_internal.h`), the frame being drawn and what every part binds:

- `MeshBatcher` (`renderer_3d_meshes.cpp`) culls the frame's meshes against the view, sorts them, gives them their
  looks, uploads their instances and draws them, in the view's pass and into the camera's depth.
- `ShadowRenderer` (`renderer_3d_shadows.cpp`) draws the directional light's cascades and the tiles of point and spot
  lights, culling the casters of each view.
- `SkyRenderer` (`renderer_3d_sky.cpp`) draws the sky and captures the light it gives surfaces.
- `WorldCuller` (`renderer_3d_worlds.cpp`) keeps render worlds on the GPU in batches and, where the device has compute
  and indirect draws, culls their instances there.

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

### Render worlds

A `RenderWorld` (`engine/spot/render/render_world.h`) holds what stays from frame to frame: meshes are added once
(`AddMesh()`, which returns a `RenderObjectId`), changed only when they change (`SetTransform()`, `UpdateMesh()`) and
removed, and `Renderer3D::DrawWorld()` draws every one on a layer the camera sees. Each object keeps its world bounds,
index range and whether its transform mirrors, worked out when it changes rather than each frame; materials are read
when drawn, so edits to them show at once. Objects are kept side by side and ids stay safe after removal, so a game
keeping its objects elsewhere, such as in an ECS, fills one the same way. Skinned meshes, whose poses change every
frame, are still handed over each frame with `DrawSkinnedMesh()`.

With `Renderer3DConfig::gpuCulling` (`SceneRendererConfig::gpuCulling`, or `gpuCulling` in the game's rendering
settings), off by default, renderers keep each world they draw on the GPU: its instances in a buffer of their own, in
batches of up to 128 objects drawn the same way (same layer, material, mesh and range, mirroring and shadows) and near
each other, along a Morton curve through the world. A frame then costs the CPU a command per batch, not per object:
views leave out the batches they cannot see, and the GPU culls the instances of the others. Where compute shaders and
indirect draws are available, a compute pass (`world_cull.comp.hlsl`) runs a group per batch that keeps the instances in
view, of the level of detail their screen size picks, and not hidden behind what the last frame of the same view drew,
writes them side by side in their order, so the image does not change from frame to frame, and writes the batch's
indirect draw, which the view's pass then draws: instances culled cost the GPU nothing. Hidden means behind the farthest
depth of every 16-pixel tile the instance covers in the last frame's depth (`hiz_reduce.frag.hlsl`), as that frame's
view saw it; an object a frame reveals shows from the next. Elsewhere the mesh shaders cull the instances themselves
(`Instance::bounds` and the frame's `cullPlanes`), drawing those outside as nothing. Shadow views cull the instances of
the batches they reach on the CPU into their casters, since small views, such as a point light's faces, see few of them.
Only what changed goes up again, in runs of neighbors, as the world's change stamps tell; adding, removing or restyling
objects rebuilds the batches. Transparent materials are drawn object by object, sorted, as `DrawMesh()` does, and a
renderer lets go of worlds it has not drawn for 120 frames. A batch the CPU keeps counts whole in `visibleMeshes`.
Without it, a world's objects are drawn one by one, as `DrawMesh()` draws, culled and given their level of detail on the
CPU: simpler and as fast for the few hundred objects most scenes hold, while GPU culling spares the CPU a command per
object in scenes of thousands.

Objects of a world can have levels of detail (`RenderMeshDesc::lods`, `MeshRenderer::SetLods()`): up to three
simpler meshes, each shown once the object's screen size (`GetScreenSize()`: the height of its bounds as a fraction
of the view's) is below its own `screenSize`, and `cullScreenSize`, below which nothing is drawn. Each level is an
instance of its own, batched with the others of its mesh, and the shaders draw the one whose range the size falls in,
as the view's camera sees it, in shadows too, so they fall from the same mesh. Levels draw their whole mesh, and are
chosen by the bounds of the object's own.

Scenes keep theirs in a `RenderWorldSystem` (`engine/spot/scene/rendering/render_world_system.h`), which `DrawScene()`
creates and syncs: it visits the scene's `MeshRenderer`s, and takes from each only what changed since the last sync,
as `Transform::GetVersion()` (which changes when the transform or an ancestor moves) and the renderer's own changes
tell, then removes the objects of renderers that went away or were disabled. That full visit only happens when what
the scene holds changed (`Scene::GetStructureVersion()`); otherwise it visits only the entities the scene recorded as
moved or changed (`Scene::TakeChangedEntities()`, fed by transforms and `Component::MarkChanged()`), so a large world
that mostly stands still costs next to nothing. Graphics never sees the scene.

### Custom surfaces

A game whose geometry is kept in buffers of its own, such as terrain, draws it with its own shaders inside
`Renderer3D`'s passes, so it looks like the rest: a `CustomSurface` (`engine/spot/render/custom_surface.h`) handed to
`Renderer3D::DrawSurface()` each frame, or kept by a `SurfaceRenderer` component, which `DrawScene()` draws on the
layers the camera sees. Renderer3D calls it back in each of its passes with a `SurfaceDrawContext`: the pass, the
view and its frustum planes for culling, and what pipelines must match there.

- **Lit**: in the view's pass, after the sky and before the meshes, with the view and the frame's lighting already
  bound, so a surface binds only its pipeline, buffers, textures and uniforms of its own. Its vertex shader reads the
  view from `surface_frame.hlsl`; its fragment shader includes `surface_lighting.hlsl`, which declares every resource
  of the pass at the registers Renderer3D binds and shades a point as the meshes are (`ShadeOpaqueSurface()`, or
  `LightSurface()` for shading of its own). Five texture slots and the fragment uniforms at `b1` are the surface's
  own; `mesh.frag.hlsl` is itself built this way.
- **Shadow**: each view of a shadow-casting light, depth only, with the depth clamp and bias of Renderer3D's own
  casters (`GetShadowDepthState()`).
- **Depth**: the camera's depth, when `RenderCameraDepth()` draws it for effects that read it.

Before opening a pass, Renderer3D calls `CustomSurface::Prepare()` with a `SurfacePrepareContext`: the render
context, the view's kind, matrix and frustum planes, and its `viewIndex`. A surface can dispatch compute here to
cull its geometry into indirect draw arguments. All the views of a shadow atlas are prepared before the atlas's
pass opens, so each needs separate output until its draw callback. `SurfaceDrawContext::viewIndex` identifies the
matching preparation; it starts over at `Begin()` and keeps advancing through subsequent `RenderCameraDepth()`
calls. Preparation is optional; surfaces with CPU culling only implement their draw callbacks.

Drawn in the view's pass, surfaces also hide what is behind them from the occlusion culling of render worlds. Their
draws are their own: Renderer3D's statistics do not count them.

### SceneRenderer

`SceneRenderer::Render(context, scene, target)` draws one scene into the window or a texture. It draws meshes
through the first main camera (`FindMainCamera()`), then the shapes queued with `DebugDraw` (see
[Diagnostics](diagnostics.md)), then sprites and particles, all as light into an HDR image (`Rgba16Float`,
multisampled where the GPU allows). A tonemapper turns it into the target's colors, and the scene's interfaces
go over it (`UiSystem::DrawUi()`, which calls each active component's `OnDrawUi()` in pixels from the view's
bottom-left corner). An application's `RenderModule` (`scene/rendering/render_module.h`) draws its active scene this way
unless `RenderModuleConfig::drawScene` is off; the editor's game view uses the same renderer. Each frame is a render graph (see
[Render graphs](#render-graphs)): scene, sprites, particles, post-processing, FXAA and interface passes, whose
HDR, depth and tonemapped targets the graph keeps at the frame's size.

### Render hooks

Games and systems draw passes of their own at fixed points of a frame (`RenderStage`): `BeforeOpaque` (into the
cleared HDR image; a sky draws over it), `AfterOpaque` (with the surfaces' depth; where `DebugDraw`'s shapes are
drawn), `AfterTransparent`, `BeforePostProcessing`, `AfterPostProcessing` (into the target, in its colors, before the
interfaces) and `Overlay` (over everything). A hook gets a `RenderStageContext`: the camera and view-projection, a
`RenderPassDesc` into the stage's targets that keeps what they hold (depth included before post-processing), the
format and sample count its pipelines must match, the depth target, and the HDR image so far.

```cpp
void OnEnable() override {
    m_hook = GetScene().GetOrAddSystem<Spot::RenderHookSystem>().AddRenderHook(Spot::RenderStage::AfterOpaque, [this](const Spot::RenderStageContext& stage) {
        auto pass = stage.context.BeginRenderPass(stage.pass);
        // ... draw with pipelines made for stage.colorFormat and stage.sampleCount ...
    });
}
void OnDisable() override { GetScene().GetOrAddSystem<Spot::RenderHookSystem>().RemoveRenderHook(m_hook); }
```

`RenderHookSystem::AddRenderHook()` hooks run wherever the scene is drawn, the editor's views included; `SceneRenderer::AddHook()`
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

The look of a game comes from the first active `PostProcessing` component in the scene: its tonemapper
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
- `SurfaceRenderer`: a custom surface made by code (see [Custom surfaces](#custom-surfaces)), which scenes save
  without it.
- `MeshRenderer`: a mesh with a material at its entity's world transform, with optional levels of detail
  (`lods`, `cullScreenSize`; see [Render worlds](#render-worlds)). Materials (`Material`) are
  metallic-roughness PBR, as glTF defines it, so models look as their authors made them: a base color, `metallic`
  (0 for dielectrics, 1 for metals) and `roughness` (0 for a mirror, 1, the default, for a matte surface), each with
  an optional texture that multiplies it (roughness in green and metallic in blue of one texture, as glTF packs
  them); a tangent-space normal map with its `normalScale`; an occlusion map, which darkens ambient light by
  `occlusionStrength`; and an `emissive` color and texture the surface gives off itself, which glow through bloom
  above 1. Dielectrics scatter their color and reflect about 4% of the light untinted; metals scatter nothing and
  reflect in their color. Highlights are GGX microfacets, as wide as the roughness says. Materials can also be
  double-sided, and project their textures from world space (`worldTextureSize`), their normal maps then tilting
  the surface along the world axes the texture is laid out on. Projected from the world, blocks side by side continue
  each other's pattern, but the textures stay put as a mesh moves and slide across it, so meshes that move project
  from their own space instead (`MeshRenderer::SetTextureSpace()`, `RenderMeshDesc::textureSpace`): along their axes
  from their origin, at the world's scale, so the textures keep their size however the mesh is scaled. The instance
  data says which, in the normal matrix's last column, which turns nothing (see `GetNormalMatrix()`), and the vertex
  shaders hand the fragment shader the position, normal and axes the textures are projected by.
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
  two; `PrimitiveMeshes` call it, and so must code that builds meshes itself and uses normal maps. Triangles with
  two corners in one place are left out of MikkTSpace, which searches every triangle for each of them, and their
  corners take their vertices' tangents: a mesh with many, as grass whose blades end in a point, would otherwise take
  minutes to import.
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
times 600 frames and prints their mean, median, 95th percentile and worst times, the last frame's counts of meshes, draw
calls and lights, the GPU's time for shadows and the opaque pass (`Renderer3D` times them with GPU timers), and the
costliest profiler scopes. `--grid 316` makes it about 100,000 meshes, and `--lods 1` gives its spheres a coarser level
of detail. It culls on the GPU (`Renderer3DConfig::gpuCulling`), and `--gpu-culling 0` on the CPU. Run its Release build
when rendering changes, and compare with the times of the build before the change:

```sh
cmake --build --preset msvc-release --target spot_benchmark
./build/msvc/tests/Release/spot_benchmark      # --frames <count> --warmup <count> --grid <meshes on a side> --lods 1
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
See [Particles](particles.md) for the code-only renderer (`ParticleRenderer`, which draws plain `Particle` arrays with `ParticleDrawSettings`), sorting, flipbooks and readable camera depth.
`Renderer3D::RenderCameraDepth()` exposes an optional single-sample opaque/cutout depth pass from the
last frame's submissions, useful when a consumer needs readable depth from a multisampled view.

Games can apply per-user quality with `Application::SetRenderingSettings()` without resetting input bindings or changing project defaults. The renderer is rebuilt at the next scene render; `GetRenderingSettings()` and `GetRenderingRevision()` let custom hosts follow the same runtime configuration. The editor game view uses the public SceneRenderer path and follows these changes too.

Skinned meshes use `SkinnedMeshRenderer` or `Renderer3D::DrawSkinnedMesh()` with a second
vertex stream and per-instance joint palettes. Color, shadows, readable camera depth and
editor picking use the same deformation. Bounds padding covers animation beyond the bind mesh;
see [animation](animation.md).

## CPU geometry in the foundation

`MeshData`, its vertices and submeshes live in `spot/graphics/mesh_data.h`. Procedural generation, normals,
tangents and bounds are CPU operations and need no graphics device. `BoundingBox` and `BoundingSphere` are in
`spot/core/math/bounds.h`. Start there when sharing geometry between rendering, physics and custom tools;
`Mesh::Create()` in the rendering API uploads the CPU geometry when drawing is needed.

## Asset loader setup

Before loading GPU or rendering assets, call `RegisterGraphicsAssets(assets, device)` and
`RegisterRenderAssets(assets, device)`. The graphics layer owns texture decoding and GPU creation; rendering owns
meshes, materials, models, fonts and sprite atlases. Register animation separately for animated models. Decoding,
font rasterization and unsupported texture-block decompression run on workers; GPU creation and reference resolution
run when the calling thread finishes the load. Loaders are local to the supplied manager.
