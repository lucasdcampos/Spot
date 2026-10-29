// Engine-internal: what Renderer3D's mesh vertex shaders (mesh.vert.hlsl, skinned_mesh.vert.hlsl) give its mesh
// fragment shaders, which all declare it in this order so their inputs line up, and the UVs those read the
// material's textures at.
#ifndef SPOT_MESH_INPUT_HLSL
#define SPOT_MESH_INPUT_HLSL

struct MeshFragmentInput {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    // What projected textures are projected by, and the world directions of the axes they are projected along.
    float3 texturePosition : TEXCOORD4;
    float3 textureNormal : TEXCOORD5;
    float3 textureAxisX : TEXCOORD6;
    float3 textureAxisY : TEXCOORD7;
    float3 textureAxisZ : TEXCOORD8;
};

// Projects along the axis the surface faces most, of the world or of the mesh, repeating every textureSize units,
// with texture rows going down on walls like on screen.
float2 GetWorldUv(float3 position, float3 normal, float textureSize) {
    const float3 facing = abs(normal);
    const float3 p = position / textureSize;
    if (facing.x >= facing.y && facing.x >= facing.z) {
        return float2(p.z, -p.y);
    }
    if (facing.y >= facing.z) {
        return p.xz;
    }
    return float2(p.x, -p.y);
}

// The mesh's own UVs, or, for a positive worldTextureSize, the projected ones (see TextureSpace).
float2 GetMeshUv(MeshFragmentInput input, float worldTextureSize) {
    return worldTextureSize > 0.0 ? GetWorldUv(input.texturePosition, normalize(input.textureNormal), worldTextureSize)
                                  : input.uv;
}

#endif
