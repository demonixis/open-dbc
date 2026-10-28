// Studio Scene view meshes: a preview of the fixed-function look, not the engine's
// renderer. Registers follow SDL_GPU's SPIR-V layout: vertex uniforms in space1,
// fragment textures and samplers in space2, fragment uniforms in space3.

#define MAX_LIGHTS 8

struct VertexInput
{
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 world : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
};

cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 ViewProj;
    float4x4 World;
    float4x4 NormalWorld;
};

Texture2D Albedo : register(t0, space2);
SamplerState AlbedoSampler : register(s0, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 BaseColor;  // diffuse tint and alpha
    float4 Emissive;   // rgb
    float4 Specular;   // rgb, w = power
    float4 Ambient;    // rgb
    float4 Camera;     // xyz
    float4 Fog;        // rgb, w = distance (0 = off)
    float4 Flags;      // x = textured, y = lit, w = light count
    float4 LightPosition[MAX_LIGHTS];  // xyz, w = 0 directional, 1 point, 2 spot
    float4 LightDirection[MAX_LIGHTS]; // xyz, w = range
    float4 LightColor[MAX_LIGHTS];     // rgb
    float4 LightCone[MAX_LIGHTS];      // x = cos inner, y = cos outer
};

Varyings VSMain(VertexInput input)
{
    Varyings output;
    const float4 world = mul(World, float4(input.pos, 1.0));
    output.world = world.xyz;
    output.position = mul(ViewProj, world);
    output.normal = mul((float3x3)NormalWorld, input.normal);
    output.uv = input.uv;
    return output;
}

float4 PSMain(Varyings input) : SV_Target
{
    float4 albedo = BaseColor;
    if (Flags.x > 0.5)
        albedo *= Albedo.Sample(AlbedoSampler, input.uv);

    float3 color = albedo.rgb;
    const float3 toCamera = normalize(Camera.xyz - input.world);
    if (Flags.y > 0.5)
    {
        float3 normal = normalize(input.normal);
        // Faces are drawn from both sides, so light the side that is seen.
        if (dot(normal, toCamera) < 0.0)
            normal = -normal;
        float3 diffuse = Ambient.rgb;
        float3 highlight = float3(0.0, 0.0, 0.0);
        const int count = (int)Flags.w;
        for (int index = 0; index < MAX_LIGHTS; ++index)
        {
            if (index >= count)
                break;
            float3 toLight = -LightDirection[index].xyz;
            float attenuation = 1.0;
            if (LightPosition[index].w > 0.5)
            {
                const float3 offset = LightPosition[index].xyz - input.world;
                const float distance = length(offset);
                toLight = offset / max(distance, 0.0001);
                attenuation = saturate(1.0 - distance / max(LightDirection[index].w, 0.0001));
                if (LightPosition[index].w > 1.5)
                {
                    const float cone = dot(-toLight, LightDirection[index].xyz);
                    attenuation *= smoothstep(LightCone[index].y, LightCone[index].x, cone);
                }
            }
            const float lambert = saturate(dot(normal, toLight));
            diffuse += LightColor[index].rgb * lambert * attenuation;
            if (lambert > 0.0)
            {
                const float3 halfway = normalize(toLight + toCamera);
                highlight += LightColor[index].rgb * pow(saturate(dot(normal, halfway)), max(Specular.w, 1.0)) *
                             attenuation;
            }
        }
        color = albedo.rgb * diffuse + Specular.rgb * highlight;
    }
    color += Emissive.rgb;
    if (Fog.w > 0.0)
        color = lerp(color, Fog.rgb, saturate(length(Camera.xyz - input.world) / Fog.w));
    return float4(color, albedo.a);
}
