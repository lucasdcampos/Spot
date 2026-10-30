// Where the sky's clouds are, from their noise (render/cloud_noise.h): what their layer, their volume and the shadows
// they cast on surfaces share. Only functions of values, so any shader can include it.
#ifndef SPOT_CLOUD_SHAPES_HLSL
#define SPOT_CLOUD_SHAPES_HLSL

// How many times finer than the shapes the billows and wisps that carve their edges are, and how many times wider the
// weather is; neither a whole number, so their textures do not repeat together.
static const float DetailScale = 3.1;
static const float WeatherScale = 0.13;
// How deep into a cloud, from 0 at its edge to 1 at its densest, the billows carve it.
static const float ErosionDepth = 0.6;
// How far the weather pushes the coverage up or down, at half coverage: enough to clear whole regions of the sky and
// to gather others into continuous masses.
static const float WeatherStrength = 1.7;

// Where the weather gathers clouds they cover more of the sky, and where it thins them out, less, down to clear sky:
// none at a coverage of 0 and all of it at 1.
float GetLocalCoverage(float coverage, float weather) {
    const float gathered = smoothstep(0.1, 0.9, weather) - 0.5;
    return saturate(coverage + gathered * WeatherStrength * 2.0 * min(coverage, 1.0 - coverage));
}

// How tall clouds grow where the weather left this local coverage: small and flat where they are few, towering where
// they gather.
float GetCloudGrowth(float local) {
    return smoothstep(0.2, 0.9, local);
}

// The shapes' noise above this is cloud. As the noise spreads evenly, that is about the coverage's share of the sky,
// taken a little larger for what the billows carve away, and all of it at full coverage.
float GetThreshold(float coverage) {
    return 1.0 - coverage * 1.15;
}

// How far inside a cloud a point is, from 0 at its edge to 1 where it is densest.
float GetInside(float shape, float threshold) {
    return saturate((shape - threshold) / (1.0 - threshold));
}

// Where a point of the clouds, in clouds from the wind's origin, lies on the weather's noise.
float2 ToWeatherUv(float2 uv) {
    return float2(0.8 * uv.x - 0.6 * uv.y, 0.6 * uv.x + 0.8 * uv.y) * WeatherScale;
}

// Where it lies on the noise read a second time for the large formations: coarser, turned, and stretched along one
// way, so clouds also join into long streets and broad masses rather than all being alike.
float2 ToFormationUv(float2 uv) {
    return float2(0.6 * uv.x + 0.8 * uv.y, -0.8 * uv.x + 0.6 * uv.y) * float2(0.29, 0.41);
}

// The clouds' shape from the noise read at both scales: mostly the formations, broken up by the single clouds. The
// mix narrows the noise's spread, which the contrast spreads evenly again, so coverage keeps its meaning.
float CombineShapes(float formation, float single) {
    return saturate((0.6 * formation + 0.4 * single - 0.5) * 1.4 + 0.5);
}

// The clouds' density: their shapes, with the thin outer parts carved into rounded lumps by the billows, or frayed
// into wisps for soft clouds; an overcast sky keeps them whole.
float GetDensity(float shape, float4 detail, float threshold, float coverage, float softness) {
    const float carving = lerp(detail.g, detail.b, softness * softness);
    const float erosion = (1.0 - carving) * ErosionDepth * (1.0 - pow(coverage, 4.0));
    return saturate((GetInside(shape, threshold) - erosion) / (1.0 - erosion));
}

#endif
