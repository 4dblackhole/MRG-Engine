struct RectangleInstance
{
    float4 Bounds;
    float4 Color;
    row_major float4x4 Transform;
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
    const float2 localPosition =
        rectangle.Bounds.xy + Corners[vertexId] * rectangle.Bounds.zw;
    const float3 pixelPosition = mul(
        float4(localPosition, 0.0F, 1.0F),
        rectangle.Transform).xyz;
    const float2 normalizedDeviceCoordinates = float2(
        pixelPosition.x / ViewportSizePixels.x * 2.0F - 1.0F,
        1.0F - pixelPosition.y / ViewportSizePixels.y * 2.0F);

    PixelInput output;
    output.Position = float4(normalizedDeviceCoordinates, pixelPosition.z, 1.0F);
    output.Color = rectangle.Color;
    return output;
}

static const uint NoTextureIndex = 0xFFFFFFFFu;

struct VisualInstance
{
    float4 Bounds;
    float4 Color;
    row_major float4x4 Transform;
    float4 UvTransform;
    uint TextureIndex;
    uint3 Padding;
};

StructuredBuffer<VisualInstance> VisualInstances : register(t0);
Texture2D VisualTextures[64] : register(t0);
SamplerState VisualSampler : register(s0);

struct ImagePixelInput
{
    float4 Position : SV_POSITION;
    float2 Uv : TEXCOORD0;
    float4 Color : COLOR0;
    nointerpolation uint TextureIndex : TEXCOORD1;
};

ImagePixelInput ImageVSMain(
    uint vertexId : SV_VertexID,
    uint instanceId : SV_InstanceID)
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

    const VisualInstance visual = VisualInstances[instanceId];
    const float2 localPosition =
        visual.Bounds.xy + Corners[vertexId] * visual.Bounds.zw;
    const float3 pixelPosition = mul(
        float4(localPosition, 0.0F, 1.0F),
        visual.Transform).xyz;
    const float2 normalizedDeviceCoordinates = float2(
        pixelPosition.x / ViewportSizePixels.x * 2.0F - 1.0F,
        1.0F - pixelPosition.y / ViewportSizePixels.y * 2.0F);

    ImagePixelInput output;
    output.Position = float4(normalizedDeviceCoordinates, pixelPosition.z, 1.0F);
    output.Uv = Corners[vertexId] * visual.UvTransform.xy +
        visual.UvTransform.zw;
    output.Color = visual.Color;
    output.TextureIndex = visual.TextureIndex;
    return output;
}

float4 ImagePSMain(ImagePixelInput input) : SV_TARGET
{
    if (input.TextureIndex == NoTextureIndex)
    {
        return input.Color;
    }

    return VisualTextures[NonUniformResourceIndex(input.TextureIndex)].Sample(
        VisualSampler,
        input.Uv) * input.Color;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return input.Color;
}
