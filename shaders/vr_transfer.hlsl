// Full-screen copy of an eye image into the VR swapchain, with gain and sRGB transfer.

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

Texture2D Source : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

cbuffer Transfer : register(b0, space3)
{
    float4 GainAndDestinationSrgb;
    float4 SourceSrgb;
};

Varyings VSMain(uint VertexId : SV_VertexID)
{
    Varyings Output;
    Output.uv = float2((VertexId << 1) & 2, VertexId & 2);
    Output.position = float4(Output.uv.x * 2.0 - 1.0, 1.0 - Output.uv.y * 2.0, 0.0, 1.0);
    return Output;
}

float3 EncodeSrgb(float3 Value)
{
    return select(Value > 0.0031308, 1.055 * pow(max(Value, 0.0), 1.0 / 2.4) - 0.055, 12.92 * Value);
}

float3 DecodeSrgb(float3 Value)
{
    return select(Value > 0.04045, pow((Value + 0.055) / 1.055, 2.4), Value / 12.92);
}

float4 PSMain(Varyings Input) : SV_Target0
{
    float3 Value = Source.Sample(SourceSampler, Input.uv).rgb;
    if (SourceSrgb.x != 0.0) Value = EncodeSrgb(Value);
    Value = clamp(Value * GainAndDestinationSrgb.rgb, 0.0, 1.0);
    if (GainAndDestinationSrgb.w != 0.0) Value = DecodeSrgb(Value);
    return float4(Value, 1.0);
}
