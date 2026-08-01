// Common engine material: vertex color with per-instance transform and tint.
struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float4 worldViewProjection0 : INSTANCE_WVP0;
    float4 worldViewProjection1 : INSTANCE_WVP1;
    float4 worldViewProjection2 : INSTANCE_WVP2;
    float4 worldViewProjection3 : INSTANCE_WVP3;
    float4 instanceColor : INSTANCE_COLOR;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PixelInput VSMain(VertexInput input)
{
    const float4x4 worldViewProjection = float4x4(
        input.worldViewProjection0,
        input.worldViewProjection1,
        input.worldViewProjection2,
        input.worldViewProjection3);

    PixelInput output;
    output.position = mul(float4(input.position, 1.0F), worldViewProjection);
    output.color = input.color * input.instanceColor;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return input.color;
}
