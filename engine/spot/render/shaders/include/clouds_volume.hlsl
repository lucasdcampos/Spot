// What the passes that draw the clouds as a volume share (clouds_volume, clouds_resolve and clouds_composite): the
// frame's view and the layer's place. A shader that uses other uniforms first defines CLOUD_FRAME_REGISTER after them.

#ifndef CLOUD_FRAME_REGISTER
#define CLOUD_FRAME_REGISTER b0
#endif

cbuffer CloudFrame : register(CLOUD_FRAME_REGISTER, space3) {
    // Of the camera's rotation and projection only: from clip space to directions and depths, and the last frame's from
    // directions to clip space.
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    // Where what is seen now was a frame ago, relative to the camera then, less where it is now relative to the camera:
    // the camera's move, less the wind's; and 1 in w when there is a frame before to blend with.
    float4 reprojection;
    // The camera's height, and the altitudes of the layer's base and top, in meters.
    float4 layer;
    // The size of the clouds' image in pixels, half the view's, and its inverse; and the same of the view.
    float4 cloudSize;
    float4 viewSize;
};

// The planet's radius in meters, as the sky's (PlanetRadius in sky.hlsl).
static const float CloudsPlanetRadius = 6360000.0;

// The direction from the camera through a point of the view, from its top-left corner at (0, 0).
float3 GetRayDirection(float2 uv) {
    const float4 far = mul(inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0));
    return normalize(far.xyz / far.w);
}

// How far from the camera the surface at a point of the view is, from the depth stored there; very far for the sky.
float GetSceneDistance(float2 uv, float depth) {
    if (depth >= 1.0) {
        return 1e9;
    }
    const float4 surface = mul(inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
    return length(surface.xyz / surface.w);
}

// Where the ray enters and leaves the sphere at the altitude, around the planet's center under the camera, which is at
// the height; false when it misses. The roots are taken so neither cancels out.
bool IntersectAltitude(float height, float altitude, float mu, out float2 hits) {
    hits = 0.0;
    const float b = (CloudsPlanetRadius + height) * mu;
    const float c = (height - altitude) * (2.0 * CloudsPlanetRadius + height + altitude);
    const float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return false;
    }
    const float q = -b - (b >= 0.0 ? 1.0 : -1.0) * sqrt(discriminant);
    const float other = abs(q) > 1e-6 ? c / q : q;
    hits = float2(min(q, other), max(q, other));
    return true;
}

// The part of the ray inside the layer, between its base and its top and before the ground; false when there is none.
bool GetCloudSegment(float height, float bottom, float top, float mu, out float2 segment) {
    segment = 0.0;
    float2 ground;
    if (mu < 0.0 && height < bottom &&
        (height <= 0.0 || (IntersectAltitude(height, 0.0, mu, ground) && ground.x > 0.0))) {
        return false;
    }
    float2 lower;
    float2 upper;
    const bool meetsBase = IntersectAltitude(height, bottom, mu, lower) && lower.x > 0.0;
    if (!IntersectAltitude(height, top, mu, upper)) {
        return false;
    }
    if (height < bottom) {
        // From below, in through the base and out through the top.
        segment = float2(lower.y, upper.y);
    } else if (height <= top) {
        // Inside, from the camera out through the top, or down through the base.
        segment = float2(0.0, meetsBase ? lower.x : upper.y);
    } else {
        // From above, rays going down come in through the top and leave through the base, or through the top again.
        if (upper.x < 0.0) {
            return false;
        }
        segment = float2(upper.x, meetsBase ? lower.x : upper.y);
    }
    return segment.y > segment.x;
}
