// The view that Renderer3D's pass draws through, as its own mesh shader reads it, for the vertex shaders of custom
// surfaces (Renderer3D::DrawSurface()): Renderer3D sets these uniforms before the surfaces draw, so a surface's vertex
// shader reads the camera's view without pushing it. A shader including this declares its own uniforms, if any, from
// b1 of space1 on.
#ifndef SPOT_SURFACE_FRAME_HLSL
#define SPOT_SURFACE_FRAME_HLSL

cbuffer Frame : register(b0, space1) {
    float4x4 viewProjection;
    // The planes of the view, each with its normal inward: a point is in view when it is on the inner side of all six.
    float4 cullPlanes[6];
    // Where the camera is in xyz; how it sizes things in w (see Renderer3D's mesh shader).
    float4 lodView;
};

#endif
