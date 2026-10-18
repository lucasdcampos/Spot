// Culls the instances of a render world's batches, one batch per group: each keeps those in view, of the level of
// detail their screen size picks, and not hidden behind what the last frame of the view drew, writes them side by
// side in their order, so what is drawn does not change from frame to frame, and writes the batch's indirect draw.
cbuffer Cull : register(b0, space2) {
    float4 cullPlanes[6];
    // Where the camera that picks levels of detail is, and how it sizes things (see InSizeRange()).
    float4 lodView;
    // The last frame's view, which the occlusion tiles were drawn from.
    float4x4 occlusionViewProjection;
    // x: whether to test occlusion; y: the tile's size in texels; zw: the last frame's view in texels.
    uint4 occlusion;
};

struct Instance {
    float4x4 model;
    float4x4 normalMatrix;
    uint4 skin;
    float4 bounds;
};

Texture2D<float> tiles : register(t0, space0);
SamplerState tileSampler : register(s0, space0);
StructuredBuffer<Instance> instances : register(t1, space0);
// Each batch's first instance, instance count, index count and first index.
StructuredBuffer<uint4> batches : register(t2, space0);
RWStructuredBuffer<uint> visible : register(u0, space1);
// DrawIndexedIndirectArguments per batch: index count, instance count, first index, vertex offset, first instance.
RWStructuredBuffer<uint> arguments : register(u1, space1);

static const uint BatchSize = 128;
// The most tiles a sphere's rectangle is tested over; larger ones are taken as seen.
static const int MaxTiles = 4;

groupshared uint kept[BatchSize];
groupshared uint offsets[BatchSize];

bool IsInView(float4 bounds) {
    [unroll] for (int i = 0; i < 6; ++i) {
        if (dot(cullPlanes[i].xyz, bounds.xyz) + cullPlanes[i].w < -bounds.w) {
            return false;
        }
    }
    return true;
}

// The level of detail an instance is of is in skin.zw, which world instances keep no skin in.
bool InSizeRange(float4 bounds, uint4 skin) {
    const float distance = length(bounds.xyz - lodView.xyz);
    const float size = lodView.w < 0.0 ? bounds.w * -lodView.w
                                       : (distance <= bounds.w ? 1.0 : bounds.w * lodView.w / distance);
    return size >= asfloat(skin.z) && size < asfloat(skin.w);
}

// Whether the sphere, as the last frame's view saw it, is behind the farthest depth of every tile it covers.
bool IsHidden(float4 bounds) {
    float2 low = float2(1e30, 1e30);
    float2 high = float2(-1e30, -1e30);
    float nearest = 1.0;
    [unroll] for (int i = 0; i < 8; ++i) {
        const float3 corner = bounds.xyz + bounds.w * float3((i & 1) ? 1 : -1, (i & 2) ? 1 : -1, (i & 4) ? 1 : -1);
        const float4 clip = mul(occlusionViewProjection, float4(corner, 1.0));
        // Crossing the near plane, it may be anywhere in view.
        if (clip.w <= 0.0) {
            return false;
        }
        const float3 ndc = clip.xyz / clip.w;
        low = min(low, ndc.xy);
        high = max(high, ndc.xy);
        nearest = min(nearest, ndc.z);
    }
    // Out of the last view, nothing hid it there.
    if (any(high < -1.0) || any(low > 1.0) || nearest < 0.0) {
        return false;
    }
    const float2 size = float2(occlusion.zw);
    // Texels from the top-left corner, as the depth was drawn.
    const float2 from = (float2(low.x, -high.y) * 0.5 + 0.5) * size;
    const float2 to = (float2(high.x, -low.y) * 0.5 + 0.5) * size;
    uint tileWidth;
    uint tileHeight;
    tiles.GetDimensions(tileWidth, tileHeight);
    const int2 first = clamp(int2(from / occlusion.y), int2(0, 0), int2(tileWidth, tileHeight) - 1);
    const int2 last = clamp(int2(to / occlusion.y), int2(0, 0), int2(tileWidth, tileHeight) - 1);
    if (last.x - first.x >= MaxTiles || last.y - first.y >= MaxTiles) {
        return false;
    }
    float farthest = 0.0;
    for (int y = first.y; y <= last.y; ++y) {
        for (int x = first.x; x <= last.x; ++x) {
            farthest = max(farthest, tiles.Load(int3(x, y, 0)));
        }
    }
    return nearest > farthest;
}

[numthreads(128, 1, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    const uint4 batch = batches[group.x];
    const uint i = thread.x;
    bool keep = false;
    if (i < batch.y) {
        const Instance instance = instances[batch.x + i];
        keep = IsInView(instance.bounds) && InSizeRange(instance.bounds, instance.skin) &&
               (occlusion.x == 0 || !IsHidden(instance.bounds));
    }
    kept[i] = keep ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();
    // Counted in order by one thread, so the kept instances keep their order.
    if (i == 0) {
        uint total = 0;
        for (uint j = 0; j < BatchSize; ++j) {
            offsets[j] = total;
            total += kept[j];
        }
        const uint at = group.x * 5;
        arguments[at + 0] = batch.z;
        arguments[at + 1] = total;
        arguments[at + 2] = batch.w;
        arguments[at + 3] = 0;
        arguments[at + 4] = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    if (keep) {
        visible[batch.x + offsets[i]] = batch.x + i;
    }
}
