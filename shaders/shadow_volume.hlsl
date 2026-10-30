// Shadow volumes count their faces in the stencil against the scene's depth: they write no colour, so they need no
// varyings, lighting or textures.

cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 ViewProj;
};

cbuffer ModelUniforms : register(b1, space1)
{
    float4x4 ModelView;
};

float4 VSMain([[vk::location(0)]] float3 Position : POSITION) : SV_Position
{
    return mul(ViewProj, mul(ModelView, float4(Position, 1.0)));
}

float4 PSMain() : SV_Target0
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
