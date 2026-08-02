struct RectangleInstance
{
    float2 PositionPixels;
    float2 SizePixels;
    float4 Color;
};

StructuredBuffer<RectangleInstance> RectangleInstances : register(t0);

cbuffer ViewportConstants : register(b0)
{
    float2 ViewportSizePixels;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
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

    RectangleInstance rectangle = RectangleInstances[instanceId];
    const float2 pixelPosition =
        rectangle.PositionPixels + Corners[vertexId] * rectangle.SizePixels;
    const float2 normalizedDeviceCoordinates = float2(
        pixelPosition.x / ViewportSizePixels.x * 2.0F - 1.0F,
        1.0F - pixelPosition.y / ViewportSizePixels.y * 2.0F);

    PixelInput output;
    output.Position = float4(normalizedDeviceCoordinates, 0.0F, 1.0F);
    output.Color = rectangle.Color;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return input.Color;
}
