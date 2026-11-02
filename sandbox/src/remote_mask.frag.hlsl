cbuffer Settings : register(b0, space3) {
    float4 color;
};

float4 main() : SV_Target0 {
    return color;
}
