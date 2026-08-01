// Common engine material: independently sized Texture2D resources selected
// through an SRV descriptor array.  UINT_MAX selects vertex color only.
static const uint NoTextureIndex = 0xFFFFFFFFu;

Texture2D gTextures[64] : register(t0);
SamplerState gTextureSampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
    float4 worldViewProjection0 : INSTANCE_WVP0;
    float4 worldViewProjection1 : INSTANCE_WVP1;
    float4 worldViewProjection2 : INSTANCE_WVP2;
    float4 worldViewProjection3 : INSTANCE_WVP3;
    float4 instanceColor : INSTANCE_COLOR;
    float4 uvTransform : INSTANCE_UV_TRANSFORM;
    uint textureIndex : INSTANCE_TEXTURE_INDEX;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 vertexColor : COLOR0;
    float4 instanceColor : COLOR1;
    nointerpolation uint textureIndex : TEXCOORD1;
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
    output.uv =
        input.uv * input.uvTransform.xy + input.uvTransform.zw;
    output.vertexColor = input.color * input.instanceColor;
    output.instanceColor = input.instanceColor;
    output.textureIndex = input.textureIndex;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    if (input.textureIndex == NoTextureIndex)
    {
        return input.vertexColor;
    }

    return gTextures[NonUniformResourceIndex(input.textureIndex)].Sample(
        gTextureSampler,
        input.uv) * input.instanceColor;
}
