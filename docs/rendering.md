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
- **Pipelines** are immutable: one per combination of shaders, vertex layout, primitive type, blending, culling,
  depth state, target format and sample count, created ahead of time.
- **Passes** clear or keep their targets, can have a depth target, and with several samples resolve into a single
  sample target (`keepSamples` lets a later pass draw more over the samples).
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
[Diagnostics](diagnostics.md)), then their sprites, then their interfaces (`Scene::DrawUi()`, which calls each
active component's `OnDrawUi()` with a renderer in pixels from the view's bottom-left corner). It multisamples where
the GPU allows. The application draws its scenes with one unless `ApplicationConfig::renderScenes` is off.

## Scene components

- `Camera`: where a scene is seen from, looking down its entity's -Z; perspective or orthographic, with a
  background color.
- `MeshRenderer`: a mesh with a material at its entity's world transform; materials (`Material`) have a base color
  and texture, specular and shininess, double-sidedness and world-space texturing (`worldTextureSize`).
- `SpriteRenderer`: a texture or color on a rectangle in its entity's XY plane, for 2D.
- Lights: `AmbientLight`, `DirectionalLight`, `PointLight` and `SpotLight`. Up to `Renderer3D::MaxLights` light each
  frame. Lights can cast shadows: the first directional light through cascades, and point and spot lights through
  tiles of a shared shadow atlas.
- `Sky`: an atmosphere lit by the scene's brightest directional light, which turns from day to sunset to night as
  that light turns, and lights the scene with the sun through the air and ambient light of its own.

## Not there yet

PBR, HDR and post-processing, transparency, culling and instancing come in the roadmap's rendering milestones (M5 to
M7).
