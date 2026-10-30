# The sky, clouds, stars and the moon

The `Sky` component draws an atmosphere lit by the scene's sun (see [Rendering](rendering.md)). The same shader
(`render/shaders/sky.frag.hlsl`) draws stars and a moon at night, and a layer of clouds lies over the land, drawn over
the scene. None of them needs an asset or a mesh: they are noise and hashes evaluated for each pixel, so any game gets
a moving sky by adding components and setting them in its scene. The atmosphere, the sky's uniforms and how its light
is exposed are shared by the sky's and the clouds' shaders, in `render/shaders/include/sky.hlsl`.

## The atmosphere

The air is an Earth-like atmosphere, 60 km thick over a planet 6360 km across, with Bruneton's (2017) coefficients
for air a fifth denser and the haze of an ordinary clear day (five times his cleanest air's, which alone leaves the
sky a deep, almost spacelike blue and the sun bare):
molecules, which scatter blue the most (Rayleigh); haze, which scatters all colors alike, mostly forward (Mie,
Cornette-Shanks phase), and makes the glow around the sun; and a layer of ozone around 25 km up, which scatters nothing
but takes orange light, so the sky overhead stays blue while the sun sets and twilight turns it deep blue rather than
gray. It is integrated as Hillaire (2020) does:

- Two tables, built on the CPU (`render/atmosphere.cpp`) the first time a thickness and tint are asked for and kept:
  the **transmittance**, what crosses the air from any height in any direction (256 by 64, Bruneton's
  parameterization), and the **multiple scattering**, for each height and sun height the light scattered twice or more
  toward a point (32 by 32): the light scattered once toward it from 64 directions, with the ground's reflection, and
  the share the air around scatters again, whose series is summed in closed form. `SkyRenderer::PrepareTables()`
  uploads them as half floats when the sky's thickness or tint changes.
- Each pixel of the sky marches its ray through the air in 24 steps that start short near the viewer, and at each adds
  the sunlight that reaches the point (the transmittance table) through each phase, and the multiple scattering,
  dimmed by the air between it and the viewer, integrated over the step in closed form. The moon lights it the same
  way, from opposite the sun.

The CPU computes the same from the same tables (`Atmosphere`) to light the scene: the sunlight that reaches the ground,
the sky's irradiance and, for the fog, its light toward the horizon. So what is drawn and what lights the scene agree,
and the constants of `atmosphere.cpp` and `sky.hlsl` must match. The sky is drawn as its radiance times pi, in the units
surfaces are shown in, so its brightness against the sunlit ground is the real one, with nothing scaled to make up
for light left out.

Outdoor diffuse sky lighting adds up to 30% of the upward irradiance missing from the surface-normal irradiance to approximate
interreflection and keep faces turned away from the sun readable. Sky visibility and material occlusion still
limit this fill, which fades out on downward-facing surfaces; direct lighting and reflections retain their directional response.

## The sun

The sun's disc is `sunSize` in angular radius, a quarter of a degree by default as the real sun's. Its light is the
sunlight that crossed the air spread over its solid angle, so it is as bright as the real one for its size, capped at
4000 times that sunlight for half floats and the bloom, and darker and redder toward its edge (limb darkening). Its
edge is a pixel wide. Into an HDR target that is all: the air's haze draws the aureole around it, and the bloom, with
`BloomSettings::conserveEnergy`, spreads it into a gradual halo as a lens does, as wide and bright as the sun is
bright, so it is a small, sharp disc in a soft glow rather than a white patch. Targets that are not HDR have no bloom,
so the sun spills into a glow and a halo of its own there, a few degrees wide, and its disc shows the hue of its light
at full brightness. As it sets, the light crossing the air reddens and fades, and the disc and the glow with it. The
offscreen GPU check `HDR sun cores stay brighter than their halo under strong warm sunlight` reads linear HDR pixels at
ordinary and high sun intensity.

The sun also casts softer shadows the farther they fall from what casts them, as wide as its disc says (see Shadows in
[Rendering](rendering.md)).

### Comparing changes to the sky

The GPU test `The sky is captured in fixed reference views over a day` renders a gray floor with spheres and a post,
fogged and exposed by the sky as games set it up, at six times of day (noon, afternoon, golden hour, sunset, twilight
and night), toward the sun and away from it, and at noon straight up and down onto the shadows, into
`build/<preset>/tests/sky_references/`. Capturing them before and after a change to the sky, the exposure, the fog or
the shadows shows what it changed.

## Clouds

Add `Clouds` beside the `Sky` (any entity does; the first active one is drawn) and set it in the inspector or in code:

```cpp
auto& sky = scene.CreateEntity("Sky");
sky.AddComponent<Spot::Sky>();
auto& clouds = sky.AddComponent<Spot::Clouds>();
clouds.coverage = 0.6f;      // 0 clear, 1 overcast
clouds.height = 1500.0f;     // meters above the ground (Y 0)
clouds.size = 900.0f;        // about how wide a cloud is, in meters
clouds.windDirection = {1.0f, 0.0f, 0.3f};
clouds.windSpeed = 15.0f;    // meters per second
```

The clouds are a layer in the world, at `height` above the ground, which curves down with the planet toward the
horizon. They stay where they are as the camera moves, and can be flown over. `Renderer3D` draws them in the view's
pass after the opaque surfaces (`render/shaders/clouds.frag.hlsl`): each pixel's ray meets the layer, from below or from
above, and the shader writes the depth there, so what is nearer, a mountain or an aircraft, hides them, and they hide
what is beyond, such as the ground seen from above. Transparent meshes come after them, so glass shows them through
it. A camera crossing the layer sees it fade out, since a layer has no inside to fly through; drawn as a volume (see
below), they can be flown through.

Their shapes come from a texture of tiling noise, made on the CPU the first time a process draws clouds
(`render/cloud_noise.cpp`): smooth noise, lumpy with cells, for the shapes, `size` wide; finer cells, the billows,
that carve their edges into rounded lumps; finer smooth noise, the wisps, that frays them instead as `softness` rises;
and wide noise for the weather. Each channel is equalized, spread evenly from 0 to 1, so `coverage` is about the share
of the sky they cover, and `density` how opaque their thick parts are. The shapes are read twice
(`include/cloud_shapes.hlsl`): coarser, turned and stretched, for large formations, long streets and broad masses, and
at their own scale for single clouds, mixed so clouds of every size and kind share the sky. The weather, read far
wider still, pushes the coverage up and down by most of its range: it clears whole regions, gathers others into dense
masses and leaves isolated clouds between, and where clouds gather they grow taller. The wind's
`offset` carries them along. Where a pixel spans much of a cloud they fade into their average, filtered by the
texture's mipmaps, and far away into the haze, which the sky behind them shows, so they do not shimmer.

They are lit as the sky is, by the sunlight that crossed the air, orange at sunset, and at night by the moon's. A
cloud is a layer of the optical depth its density gives: a thick one reflects most of the light back the way it came
and lets a share through, scattered, while a thin one lets most of it through. So, seen from below, their thick parts
are gray and their thin edges bright, glowing in front of the sun with the light they scatter forward; seen from above,
they are white. The sky's light from above (`SkyLighting::skylight`) and the ground's from below light them too. They
stand as tall as they are dense, so a low sun shades the sides of each that face away from it, behind those around it.
At night the moon lights them gray against the dark sky, and they hide the stars.

### Their shadows

Clouds shade what is under them from the sun and the moon. For each directional light, the lit shaders
(`GetCloudShadow()` in `surface_lighting.hlsl`, which every mesh and custom surface goes through) follow the light's ray
from the point up to the middle of the layer, look up the same noise, with the same coverage and weather
(`include/cloud_shapes.hlsl`, shared with both ways of drawing them), and dim the light the deeper inside a cloud's
shape the ray crosses it. Full-resolution noise and the layer's shared edge erosion keep the shadow inside the
cloud outline; an optical depth of 1.5 keeps cloud darkening moderate. The final cloud-motion uniform holds softness. They drift with the
clouds, need no shadow map, and leave the light from all around as it is. What is above the clouds, or lit from
within a couple of degrees of the horizon, is not shaded. `SkyRenderer::GetCloudShadowUniforms()` and `Bind()` hand
the lit pass the clouds and their noise (slot 13).

### As a volume

With `Renderer3DConfig::volumetricClouds` (`volumetricClouds` in the rendering settings, on by default for games),
the main view of an HDR target draws the clouds as a volume between `height` and `height + thickness`, which a
camera flies through and mountains rise into. `SkyRenderer::RenderVolumetricClouds()` does it before the view's pass,
in three steps:

- **March** (`clouds_volume.frag.hlsl`), at half the view's size: each ray crosses the layer from where it enters it
  to where it leaves it, or to the camera's depth (`RenderCameraDepth()`, which ambient occlusion often drew already),
  in 56 steps. At each, the density is built in three levels (`GetCloudDensity()`), each working only within what
  the one above made, so the inside of a cloud varies as much as its edges while details never move the clouds:
  - the **macrostructure** (`GetMacroDensity()`) places them and sizes them: the weather and the layer's shapes say
    where clouds are and how tall they rise, the deeper inside a shape and the more the weather gathers them the
    higher, from a flat base that sets in within a few meters; the smooth swells of a 32³ tiling volume of noise
    (`GetCloudVolumeNoise()`, a `TextureType::Texture3D`) raise some parts into towers and hollow others out;
  - the **mesostructure** (`ApplyMesostructure()`): its billows raise bulges and dig cavities, little at the base and
    more toward the irregular tops, and thin the inside unevenly;
  - the **microstructure** (`ApplyMicrostructure()`): its fine billows, swirled by the billows as turbulence would,
    fray the edges into wisps and leave thinner pockets.

  The finer levels fade out where a pixel spans more than one of their waves, since the volume has no mipmaps, and then
  take what they would on average, known since the noise is equalized, so clouds keep their mass and silhouette near
  and far. The constants of each level are at the top of the shader, named after it. Each step in a cloud marches five
  steps toward the light to find how much cloud shades it, dimmed more than the view is so the middle of a large
  cloud lies in its own shade, and lights it with three octaves of light scattered ever more times, ever less dimmed
  and ever more evenly (Wrenninge 2013), through a phase that scatters mostly forward, kept broad and below a ceiling
  so the edges in front of the sun glow without burning out into white outlines; seen away from the light, the
  folds of the sunlit side darken ("powder", Schneider 2015). The sky's light from above and the ground's from below
  light it too, a little fainter toward the base and in dense pockets, with a share of the sunlight the cloud field
  scatters about, so no side of a cloud is black by day. Far away, the air between them and the camera gives them the color of the sky toward the
  horizon, so they lose their contrast, then fade into it slowly, to nothing before the farthest the rays reach (90
  km); the fog takes them as it does the layer.
- **Resolve** (`clouds_resolve.frag.hlsl`): each frame starts its rays a different share of a step along (interleaved
  gradient noise turned by the frame), and blends a tenth of what it finds with the frames before. Where a pixel's
  clouds were a frame ago comes from where its ray enters them, moved by the camera and against the wind; what was
  there is kept within the colors around the pixel now, so nothing lingers once hidden. A camera that jumps over a
  kilometer, or a change of size, starts over.
- **Composite** (`clouds_composite.frag.hlsl`), in the view's pass where the layer would be drawn: each pixel takes the
  four nearest pixels of the clouds, weighted by how alike the distances they were marched against are to its own, so
  they do not bleed over the edges of what is in front of them, and blends them over the opaque surfaces.

Reflection probes and views that are not HDR still draw the layer, which has the same shapes.

`Clouds` moves its offset every update by the wind; `GetOffset()`/`SetOffset()` save and restore where they are, and
the offset is saved with the scene. Changing `coverage` over time makes weather. Without a scene, set
`SkyData::clouds` (`SkyClouds`) on what `Renderer3D::DrawSky()` takes.

## Stars and the moon

Once the sun is a few degrees below the horizon, stars come out on the `Sky`: at most one per cell of a grid over the
sky, most of them faint, tinted from blue to orange, a pixel or so wide at any resolution (smaller than a pixel, a
star spreads its light over it), twinkling with time. They turn around `starAxis` by `starRotation`; a day cycle sets
the rotation from the time of day, a turn a day, so they wheel across the night with the sun's path. A full moon,
`moonSize` in angular radius, rises opposite the sun with darker seas and a faint glow, and the air near the horizon
dims both. `starBrightness` or `moonSize` at 0 hides them; a sky without a sun shows stars but no moon.

The moon also lights the scene, `moonBrightness` times as bright as a full moon looks to eyes used to the dark: a
directional light from its disc, through the air as the sun's is, so it reddens low and casts the shadows once the sun
has set, and the faint blue sky its light scatters, drawn and captured as the sun's (`GetSkyRadiance()` gives both).

Clouds, stars and the moon's disc are drawn only: the sky's light that `Renderer3D` captures to light the scene leaves
them out, so they move every frame without capturing it again.

### Comparing changes

The GPU test `Clouds are captured in fixed reference views` renders the same clouds (coverage 0.5, density 0.85,
1500 m up, 1000 m wide, 1500 m tall, no wind) from six fixed views into `build/<preset>/tests/cloud_references/`:
below at noon, below toward a low sun, at sunset, toward the horizon, from above and from inside the layer, each
after twelve frames, so the blend over frames has evened out the noise. Capturing them before and after a change to
the clouds shows what it changed, view by view.

## Where to start

- `engine/spot/scene/rendering/clouds.h` and `sky.h`: the components.
- `engine/spot/render/sky_data.h`: `SkyClouds` and `SkyNight`, what the renderer draws.
- `engine/spot/render/shaders/sky.frag.hlsl`: `GetStars()` and `GetMoon()`.
- `engine/spot/render/shaders/clouds.frag.hlsl` and `engine/spot/render/cloud_noise.cpp`: the clouds, drawn by
  `SkyRenderer::DrawClouds()` between the opaque and the transparent meshes; `include/clouds.hlsl`, what the layer and
  the volume share.
- `clouds_volume.frag.hlsl`, `clouds_resolve.frag.hlsl` and `clouds_composite.frag.hlsl`: the volume, from
  `SkyRenderer::RenderVolumetricClouds()` and `CompositeClouds()`.
- The sandbox's `DayCycle` turns the stars with its time of day, and its playground has a `Clouds` on the sun.

Sky occlusion expands its sampling footprint with the height of the cover read from the overhead depth map
(twice the gap, with the existing minimum radius). Narrow elevated canopies therefore leave much of the sky
visible, while broad roofs still occlude it. The normal offsets this footprint toward the side the surface faces.

Diffuse outdoor sky light has a gain of 2.5 on upward and lateral surfaces, fading to 1 on downward surfaces,
to approximate the indirect bounces missing from the rasterized scene. Covered surfaces use the ground light
scaled once by its reflectance, rather than twice. Direct sunlight, sky appearance and reflections keep their intensity.
