# Clouds, stars and the moon

The `Sky` component draws an atmosphere lit by the scene's sun (see [Rendering](rendering.md)). Over it, the same
shader (`render/shaders/sky.frag.hlsl`) draws a layer of clouds, and at night stars and a moon. None of them needs a
texture or a mesh: they are noise and hashes evaluated for each pixel of the sky, so any game gets a moving sky by
adding components and setting them in its scene.

The sun has an atmospheric halo and a bright core. In HDR the core adds radiance over the halo before bloom and
tonemapping, so stronger sunlight cannot turn the center darker than its surroundings. Display-range rendering
retains its hue-preserving core blend. The offscreen GPU check `HDR sun cores stay brighter than their halo under
strong warm sunlight` reads linear HDR pixels at ordinary and high sun intensity; existing sky checks cover the
display-range sun, sunset color and scene lighting.

## Clouds

Add `Clouds` beside the `Sky` (any entity does; the first active one is drawn) and set it in the inspector or in code:

```cpp
auto& sky = scene.CreateEntity("Sky");
sky.AddComponent<Spot::Sky>();
auto& clouds = sky.AddComponent<Spot::Clouds>();
clouds.coverage = 0.6f;      // 0 clear, 1 overcast
clouds.height = 1500.0f;     // meters above the viewer
clouds.size = 900.0f;        // about how wide a cloud is, in meters
clouds.windDirection = {1.0f, 0.0f, 0.3f};
clouds.windSpeed = 15.0f;    // meters per second
```

Each pixel's ray meets a flat layer at `height` above the viewer; fractal value noise there, scaled by `size` and
moved by the wind's `offset`, gives the clouds' shapes, and `coverage` sets how much of the noise counts as cloud,
with `softness` blurring its edges and `density` making its thick parts opaque. The clouds are lit as the sky is:
by the sunlight that crossed the air, so they turn orange at sunset, darker where the noise is thicker toward the sun,
glowing at thin edges in front of it, and by the sky's own light on their shaded sides. At night the moon lights them
gray against the dark sky, and they hide the stars. Far toward the horizon they fade
into the haze, and where a pixel covers more than a cloud, into their average, so they do not shimmer.

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

## Where to start

- `engine/spot/scene/rendering/clouds.h` and `sky.h`: the components.
- `engine/spot/render/sky_data.h`: `SkyClouds` and `SkyNight`, what the renderer draws.
- `engine/spot/render/shaders/sky.frag.hlsl`: `GetClouds()`, `GetStars()` and `GetMoon()`.
- The sandbox's `DayCycle` turns the stars with its time of day, and its playground has a `Clouds` on the sun.
