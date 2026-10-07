cbuffer Grid : register(b0, space3) {
    float4x4 inverseViewProjection;
    float4x4 viewProjection;
    // xyz: the camera's position; w: how far the grid reaches before it fades out.
    float4 camera;
};

struct Input {
    float4 position : SV_Position;
    float2 clip : TEXCOORD0;
};

struct Output {
    float4 color : SV_Target0;
    // The grid lies on the ground plane, so it hides behind what stands on it and in front of it.
    float depth : SV_Depth;
};

float3 Unproject(float2 clip, float depth) {
    const float4 position = mul(inverseViewProjection, float4(clip, depth, 1.0));
    return position.xyz / position.w;
}

// How much of a line of the grid of the given cell size covers the pixel, from 0 to 1. Lines fade out where they
// would get closer than a few pixels, where they would only shimmer.
float Lines(float2 coordinate, float width) {
    const float2 change = fwidth(coordinate);
    const float2 distance = abs(frac(coordinate - 0.5) - 0.5) / max(change, 1e-6);
    const float coverage = 1.0 - min(min(distance.x, distance.y) / width, 1.0);
    const float density = max(change.x, change.y);
    return coverage * saturate(1.0 - (density - 0.15) * 5.0);
}

// How much the line along an axis, where the coordinate is 0, covers the pixel.
float Axis(float coordinate, float width) {
    return 1.0 - min(abs(coordinate) / max(fwidth(coordinate) * width, 1e-6), 1.0);
}

Output main(Input input) {
    const float3 nearPoint = Unproject(input.clip, 0.0);
    const float3 farPoint = Unproject(input.clip, 1.0);
    const float run = farPoint.y - nearPoint.y;
    const float along = abs(run) > 1e-6 ? -nearPoint.y / run : -1.0;
    if (along <= 0.0) {
        discard;
    }
    const float3 world = nearPoint + along * (farPoint - nearPoint);
    const float4 clip = mul(viewProjection, float4(world, 1.0));
    const float depth = clip.z / clip.w;
    if (depth <= 0.0 || depth >= 1.0) {
        discard;
    }

    float3 color = float3(0.32, 0.33, 0.36);
    float alpha = max(Lines(world.xz, 1.0) * 0.35, Lines(world.xz * 0.1, 1.3) * 0.6);
    // The X axis runs where z is 0, red; the Z axis where x is 0, blue.
    const float xAxis = Axis(world.z, 1.6);
    const float zAxis = Axis(world.x, 1.6);
    color = lerp(color, float3(0.80, 0.16, 0.18), xAxis);
    color = lerp(color, float3(0.16, 0.36, 0.86), zAxis);
    alpha = max(alpha, max(xAxis, zAxis) * 0.85);

    // Fades with distance, and where the ground is seen edge-on, where lines crowd together.
    const float fade = saturate(1.0 - length(world.xz - camera.xz) / camera.w);
    const float3 view = normalize(world - camera.xyz);
    alpha *= fade * fade * saturate(abs(view.y) * 6.0);
    if (alpha <= 0.002) {
        discard;
    }

    Output output;
    output.color = float4(color, alpha);
    output.depth = depth;
    return output;
}
