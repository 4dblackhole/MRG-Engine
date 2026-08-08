struct GlyphInstance
{
    row_major float4x4 Transform;
    float2 PositionPixels;
    float2 SizePixels;
    float4 UvRectangle;
    float4 Color;
    float Depth;
};

Texture2D<float> GlyphAtlas : register(t0);
StructuredBuffer<GlyphInstance> GlyphInstances : register(t1);

SamplerState GlyphSampler : register(s0);

cbuffer ViewportConstants : register(b0)
{
    float2 ViewportSizePixels;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
    float2 Uv : TEXCOORD0;
    float4 Color : COLOR0;
};

PixelInput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 Corners[6] =
    {
        float2(0.0F, 0.0F),
        float2(1.0F, 0.0F),
        float2(0.0F, 1.0F),
        float2(0.0F, 1.0F),
        float2(1.0F, 0.0F),
        float2(1.0F, 1.0F)
    };

    GlyphInstance glyph = GlyphInstances[instanceId];
    float2 corner = Corners[vertexId];
    float2 pixelPosition =
        glyph.PositionPixels + corner * glyph.SizePixels;
    float4 transformedPosition = mul(
        float4(pixelPosition, 0.0F, 1.0F),
        glyph.Transform);
    float2 normalizedDeviceCoordinates = float2(
        transformedPosition.x / ViewportSizePixels.x * 2.0F - 1.0F,
        1.0F - transformedPosition.y / ViewportSizePixels.y * 2.0F);

    PixelInput output;
    output.Position = float4(
        normalizedDeviceCoordinates,
        glyph.Depth + transformedPosition.z,
        1.0F);
    output.Uv = lerp(
        glyph.UvRectangle.xy,
        glyph.UvRectangle.zw,
        corner);
    output.Color = glyph.Color;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    float coverage = GlyphAtlas.Sample(GlyphSampler, input.Uv);
    return float4(
        input.Color.rgb,
        input.Color.a * coverage);
}
