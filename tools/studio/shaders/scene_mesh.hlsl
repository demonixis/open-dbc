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
// SET BUMP MAPPING ON's tangent-space normal map, and SET CUBE MAPPING ON's cube.
Texture2D NormalMap : register(t1, space2);
SamplerState NormalSampler : register(s1, space2);
TextureCube Environment : register(t2, space2);
SamplerState EnvironmentSampler : register(s2, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 BaseColor;  // diffuse tint and alpha
    float4 Emissive;   // rgb
    float4 Specular;   // rgb, w = power
    float4 Ambient;    // rgb
    float4 Camera;     // xyz
    float4 Fog;        // rgb, w = distance (0 = off)
    float4 Flags;      // x = textured, y = lit, w = light count
    float4 Stages;     // x = normal map; y = cube map: 1 reflects, 2 multiplies
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

// Bends the normal by a tangent-space one, with a frame from the screen derivatives of the position
// and the coordinates, so that meshes need no tangents. Green follows +V, as in the engine.
float3 PerturbNormal(float3 normal, float3 position, float2 uv, float3 tangentNormal)
{
    const float3 dp1 = ddx(position);
    const float3 dp2 = ddy(position);
    const float2 duv1 = ddx(uv);
    const float2 duv2 = ddy(uv);
    const float3 dp2perp = cross(dp2, normal);
    const float3 dp1perp = cross(normal, dp1);
    const float3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
    const float3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
    const float size = max(dot(tangent, tangent), dot(bitangent, bitangent));
    if (size <= 1e-20)
        return normal;
    const float inverse = (dot(normal, cross(dp1, dp2)) < 0.0 ? -1.0 : 1.0) / sqrt(size);
    return normalize(tangentNormal.x * tangent * inverse + tangentNormal.y * bitangent * inverse +
                     tangentNormal.z * normal);
}

float4 PSMain(Varyings input) : SV_Target
{
    float4 albedo = BaseColor;
    if (Flags.x > 0.5)
        albedo *= Albedo.Sample(AlbedoSampler, input.uv);

    float3 color = albedo.rgb;
    const float3 toCamera = normalize(Camera.xyz - input.world);
    float3 normal = normalize(input.normal);
    // Faces are drawn from both sides, so light the side that is seen.
    if (dot(normal, toCamera) < 0.0)
        normal = -normal;
    float3 specular = Specular.rgb;
    if (Stages.x > 0.5)
    {
        normal = PerturbNormal(normal, input.world, input.uv,
                               normalize(NormalMap.Sample(NormalSampler, input.uv).rgb * 2.0 - 1.0));
        // What a dielectric reflects, as the engine gives a normal map without a specular colour.
        if (dot(specular, specular) <= 0.0)
            specular = float3(0.04, 0.04, 0.04);
    }
    if (Flags.y > 0.5)
    {
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
        color = albedo.rgb * diffuse;
        if (Stages.y > 0.5 && Stages.y < 1.5)
        {
            // The specular colour is what the surface reflects head on; it rises at grazing angles.
            const float gloss = saturate(log2(max(Specular.w, 1.0)) / 11.0);
            const float grazing = pow(1.0 - saturate(dot(normal, toCamera)), 5.0);
            const float limit = gloss * saturate(50.0 * max(specular.r, max(specular.g, specular.b)));
            const float3 reflectance = specular + max(limit - specular, 0.0) * grazing;
            color = lerp(color, Environment.Sample(EnvironmentSampler, reflect(-toCamera, normal)).rgb, reflectance);
        }
        color += specular * highlight;
    }
    // Without a reflection to make, the cube is a texture stage that multiplies.
    if (Stages.y > 1.5)
        color *= Environment.Sample(EnvironmentSampler, reflect(-toCamera, normal)).rgb;
    color += Emissive.rgb;
    if (Fog.w > 0.0)
        color = lerp(color, Fog.rgb, saturate(length(Camera.xyz - input.world) / Fog.w));
    return float4(color, albedo.a);
}
