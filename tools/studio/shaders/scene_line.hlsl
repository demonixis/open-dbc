// Studio Scene view lines: grid, axes, bounds and light ranges. Registers follow
// SDL_GPU's SPIR-V layout: vertex uniforms in space1.

struct VertexInput
{
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(1)]] float4 color : COLOR;
};

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : TEXCOORD0;
};

cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 ViewProj;
};

Varyings VSMain(VertexInput input)
{
    Varyings output;
    output.position = mul(ViewProj, float4(input.pos, 1.0));
    output.color = input.color;
    return output;
}

float4 PSMain(Varyings input) : SV_Target
{
    return input.color;
}
