// Fills a storage texture with the color of its uniforms.
cbuffer Fill : register(b0, space2) {
    float4 color;
};

[[spv::format_rgba8]] RWTexture2D<float4> target : register(u0, space1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width;
    uint height;
    target.GetDimensions(width, height);
    if (id.x < width && id.y < height) {
        target[id.xy] = color;
    }
}
