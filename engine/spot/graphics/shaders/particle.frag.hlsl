Texture2D<float4> particleTexture : register(t0, space2);
SamplerState particleSampler : register(s0, space2);
Texture2D<float> sceneDepth : register(t1, space2);
SamplerState depthSampler : register(s1, space2);
cbuffer Frame : register(b0, space3) {
    float4 planes; // near, far, orthographic, has depth
    float4 view; // width, height, textured
};
struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 localUv : TEXCOORD2;
    float softDistance : TEXCOORD3;
};
float LinearDepth(float depth) {
    return planes.z > 0.5 ? lerp(planes.x,planes.y,depth) : planes.x * planes.y / (planes.y - depth * (planes.y - planes.x));
}
float4 main(Input input) : SV_Target0 {
    float4 color = particleTexture.Sample(particleSampler,input.uv) * input.color;
    if (view.z < 0.5) color.a *= 1 - smoothstep(0.25,0.5,length(input.localUv - 0.5));
    if (planes.w > 0.5) {
        const float depth = sceneDepth.Sample(depthSampler,input.position.xy / view.xy);
        if (input.position.z > depth) discard;
        if (input.softDistance > 0) color.a *= saturate((LinearDepth(depth) - LinearDepth(input.position.z)) / input.softDistance);
    }
    return color;
}
