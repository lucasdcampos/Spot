// Exponential height fog, as every shader that fogs what it draws reads it. A shader that uses other uniforms first
// defines FOG_REGISTER after them.
#ifndef SPOT_FOG_HLSL
#define SPOT_FOG_HLSL

#ifndef FOG_REGISTER
#define FOG_REGISTER b1
#endif

// The fog's color, with 1 in w when there is fog; its density, start, height falloff and base height; the camera's
// position; and the light it scatters toward the viewer from the sun, with the direction toward the sun.
cbuffer Fog : register(FOG_REGISTER, space3) {
    float4 fogColor;
    float4 fogParams;
    float4 fogCamera;
    float4 fogGlow;
    float4 fogGlowDirection;
};

// How much of the light from a point the fog takes on its way to the viewer: exponential height fog, integrated in
// closed form along the ray from where the fog starts.
float GetFogAmount(float3 toPoint) {
    if (fogColor.w < 0.5) {
        return 0.0;
    }
    const float distance = length(toPoint);
    const float fogged = max(distance - fogParams.y, 0.0);
    if (fogged <= 0.0) {
        return 0.0;
    }
    const float3 direction = toPoint / max(distance, 1e-5);
    const float startHeight = fogCamera.y + direction.y * (distance - fogged) - fogParams.w;
    const float falloff = fogParams.z;
    float depth = fogParams.x * fogged;
    if (falloff > 1e-5) {
        // The fog thins along the ray as it climbs; flat rays see it as it is. The density at each end is taken on
        // its own, since a ray going far down from high above the fog would multiply a density that rounds to 0 by a
        // growth that overflows.
        const float endHeight = startHeight + direction.y * fogged;
        const float rise = falloff * (endHeight - startHeight);
        const float startDensity = exp(-falloff * startHeight);
        depth *= abs(rise) > 1e-4 ? (startDensity - exp(-falloff * endHeight)) / rise : startDensity;
    }
    return 1.0 - exp(-depth);
}

// How much more of the sun's light haze scatters toward a direction than its average: mostly forward
// (Henyey-Greenstein), so the fog glows toward the sun. Must match FogGlowAnisotropy in renderer_3d_sky.cpp.
static const float FogGlowAnisotropy = 0.6;

float GetFogGlowPhase(float cosAngle) {
    const float g = FogGlowAnisotropy;
    return (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosAngle, 1e-4), 1.5);
}

// The fog's color seen toward the direction, from the camera.
float3 GetFogColor(float3 direction) {
    return fogColor.rgb + fogGlow.rgb * GetFogGlowPhase(dot(direction, fogGlowDirection.xyz));
}

// The light from a point toward the camera, the offset from the camera to it, fogged.
float3 ApplyFog(float3 color, float3 toPoint) {
    const float amount = GetFogAmount(toPoint);
    if (amount <= 0.0) {
        return color;
    }
    return lerp(color, GetFogColor(normalize(toPoint)), amount);
}

#endif
