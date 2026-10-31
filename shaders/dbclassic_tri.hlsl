// Engine triangle shader. Registers follow SDL_GPU's SPIR-V layout: vertex uniforms in
// space1, fragment textures and samplers in space2, fragment uniforms in space3.

struct VertexInput
{
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float4 color : COLOR;
    [[vk::location(3)]] float2 uv : TEXCOORD0;
    [[vk::location(4)]] float4 box_tr : TEXCOORD1;
    [[vk::location(5)]] float4 box_br : TEXCOORD2;
    [[vk::location(6)]] float4 box_bl : TEXCOORD3;
    [[vk::location(7)]] float2 aux_uv0 : TEXCOORD4;
    [[vk::location(8)]] float2 aux_uv1 : TEXCOORD5;
    [[vk::location(9)]] float2 aux_uv2 : TEXCOORD6;
    [[vk::location(10)]] float2 aux_uv3 : TEXCOORD7;
    [[vk::location(11)]] float2 aux_uv4 : TEXCOORD8;
    [[vk::location(12)]] float2 aux_uv5 : TEXCOORD9;
    [[vk::location(13)]] float2 aux_uv6 : TEXCOORD10;
    [[vk::location(14)]] float3 cube_direction : TEXCOORD11;
};

// A GPU-resident mesh: object-space vertices, then one record per instance with the object-to-world rows, the
// normal matrix rows, the object's colour, texture scale and scroll, and its material.
struct MeshInput
{
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float4 color : COLOR;
    [[vk::location(3)]] float2 uv : TEXCOORD0;
    [[vk::location(4)]] float4 world0 : TEXCOORD1;
    [[vk::location(5)]] float4 world1 : TEXCOORD2;
    [[vk::location(6)]] float4 world2 : TEXCOORD3;
    // w: the fade multiplying the normal's length, 1 when lit (0 for a zero normal), 1 to normalise first.
    [[vk::location(7)]] float4 normal0 : TEXCOORD4;
    [[vk::location(8)]] float4 normal1 : TEXCOORD5;
    [[vk::location(9)]] float4 normal2 : TEXCOORD6;
    [[vk::location(10)]] float4 color_scale : TEXCOORD7;
    // xy scale, zw offset.
    [[vk::location(11)]] float4 uv_transform : TEXCOORD8;
    [[vk::location(12)]] float4 box_tr : TEXCOORD9;
    [[vk::location(13)]] float4 box_br : TEXCOORD10;
    [[vk::location(14)]] float4 box_bl : TEXCOORD11;
};

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 view_pos : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
    [[vk::location(3)]] float4 color : TEXCOORD3;
    [[vk::location(4)]] float4 box_tr : TEXCOORD4;
    [[vk::location(5)]] float4 box_br : TEXCOORD5;
    [[vk::location(6)]] float4 box_bl : TEXCOORD6;
    [[vk::location(7)]] float2 aux_uv0 : TEXCOORD7;
    [[vk::location(8)]] float2 aux_uv1 : TEXCOORD8;
    [[vk::location(9)]] float2 aux_uv2 : TEXCOORD9;
    [[vk::location(10)]] float2 aux_uv3 : TEXCOORD10;
    [[vk::location(11)]] float2 aux_uv4 : TEXCOORD11;
    [[vk::location(12)]] float2 aux_uv5 : TEXCOORD12;
    [[vk::location(13)]] float2 aux_uv6 : TEXCOORD13;
    [[vk::location(14)]] float3 cube_direction : TEXCOORD14;
};

cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 ViewProj;
};

// Identity for geometry already in view space; the world-to-view matrix for static world-space meshes and
// GPU-resident meshes.
cbuffer ModelUniforms : register(b1, space1)
{
    float4x4 ModelView;
};

Texture2D tex0 : register(t0, space2);
Texture2D tex1 : register(t1, space2);
Texture2D tex2 : register(t2, space2);
Texture2D tex3 : register(t3, space2);
Texture2D tex4 : register(t4, space2);
Texture2D tex5 : register(t5, space2);
Texture2D tex6 : register(t6, space2);
Texture2D tex7 : register(t7, space2);
TextureCube cube_tex : register(t8, space2);
SamplerState sampler0 : register(s0, space2);
SamplerState sampler1 : register(s1, space2);
SamplerState sampler2 : register(s2, space2);
SamplerState sampler3 : register(s3, space2);
SamplerState sampler4 : register(s4, space2);
SamplerState sampler5 : register(s5, space2);
SamplerState sampler6 : register(s6, space2);
SamplerState sampler7 : register(s7, space2);
SamplerState cube_sampler : register(s8, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 dir_light_direction;
    float4 dir_light_color;
    float4 ambient_color;
    float4 fog_color_dist;
    float4 flags;
    int4 light_counts;
    float4 point_lights[24];
    float4 spot_lights[18];
};

cbuffer TextureStageUniforms : register(b1, space3)
{
    int4 stages[8];
};

// Blinn-Phong only (stages[0].z == 4). Stage 1 holds the normal map.
cbuffer MaterialUniforms : register(b2, space3)
{
    float4 view_to_world[3];
    // x whether there is a normal map, y cube mip levels (0: no reflection), zw what stage 1 multiplies its
    // coordinates by (0: nothing).
    float4 material_params;
};

Varyings VSMain(VertexInput input)
{
    Varyings output;
    float4 view_pos = mul(ModelView, float4(input.pos, 1.0));
    output.position = mul(ViewProj, view_pos);
    output.view_pos = view_pos.xyz;
    output.normal = mul((float3x3)ModelView, input.normal);
    output.uv = input.uv;
    output.color = input.color;
    output.box_tr = input.box_tr;
    output.box_br = input.box_br;
    output.box_bl = input.box_bl;
    output.aux_uv0 = input.aux_uv0;
    output.aux_uv1 = input.aux_uv1;
    output.aux_uv2 = input.aux_uv2;
    output.aux_uv3 = input.aux_uv3;
    output.aux_uv4 = input.aux_uv4;
    output.aux_uv5 = input.aux_uv5;
    output.aux_uv6 = input.aux_uv6;
    output.cube_direction = input.cube_direction;
    return output;
}

// Draws every instance of a GPU-resident mesh as the CPU path draws each object's triangles.
Varyings VSMesh(MeshInput input)
{
    Varyings output;
    const float3x4 world = float3x4(input.world0, input.world1, input.world2);
    const float4 view_pos = mul(ModelView, float4(mul(world, float4(input.pos, 1.0)), 1.0));
    output.position = mul(ViewProj, view_pos);
    output.view_pos = view_pos.xyz;
    const float3x3 normal_matrix = float3x3(input.normal0.xyz, input.normal1.xyz, input.normal2.xyz);
    float3 normal = mul((float3x3)ModelView, mul(normal_matrix, input.normal));
    // A length too small to normalise gives +Z, here in view space; only extreme scales get here.
    if (input.normal2.w > 0.5)
        normal = dot(normal, normal) > 1e-12 ? normalize(normal) : float3(0.0, 0.0, 1.0);
    output.normal = normal * (input.normal0.w * input.normal1.w);
    output.uv = input.uv * input.uv_transform.xy + input.uv_transform.zw;
    output.color = input.color * input.color_scale;
    output.box_tr = input.box_tr;
    output.box_br = input.box_br;
    output.box_bl = input.box_bl;
    // No texture stage but the first: the others take its coordinates, as on the CPU path.
    output.aux_uv0 = output.uv;
    output.aux_uv1 = output.uv;
    output.aux_uv2 = output.uv;
    output.aux_uv3 = output.uv;
    output.aux_uv4 = output.uv;
    output.aux_uv5 = output.uv;
    output.aux_uv6 = output.uv;
    output.cube_direction = float3(0.0, 0.0, 0.0);
    return output;
}

// Gamma correction for uniform colors (light colors, fog, ambient).
// Texture samples are already linear — the GPU converts sRGB→linear
// automatically when using UNORM_SRGB texture format.

float3 srgb_to_linear(float3 c)
{
    return pow(max(c, 0.0), 2.2);
}

float3 linear_to_srgb(float3 c)
{
    return pow(max(c, 0.0), 1.0 / 2.2);
}

float3 diffuse_light(float3 N, float3 L, float3 light_color)
{
    float diff = max(dot(N, L), 0.0);
    return light_color * diff;
}

float3 specular_light(float3 N, float3 L, float3 V, float3 light_color, float spec_power)
{
    float3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), spec_power);
    return light_color * spec * 0.15;
}

// The specular term of the Blinn-Phong path, normalised so that a tighter highlight is brighter rather than dimmer. The light
// colour already folds in the pi that diffuse_light leaves out.
float3 blinn_phong_light(float3 N, float3 L, float3 V, float3 light_color, float spec_power)
{
    float3 H = normalize(L + V);
    float spec = (spec_power + 8.0) / 8.0 * pow(max(dot(N, H), 0.0), spec_power) * max(dot(N, L), 0.0);
    return light_color * spec;
}

float4 sample_2d_stage(int stage, float2 uv)
{
    if (stage == 0) return tex0.Sample(sampler0, uv);
    if (stage == 1) return tex1.Sample(sampler1, uv);
    if (stage == 2) return tex2.Sample(sampler2, uv);
    if (stage == 3) return tex3.Sample(sampler3, uv);
    if (stage == 4) return tex4.Sample(sampler4, uv);
    if (stage == 5) return tex5.Sample(sampler5, uv);
    if (stage == 6) return tex6.Sample(sampler6, uv);
    return tex7.Sample(sampler7, uv);
}

float4 apply_texture_operation(int mode, float4 arg1, float4 arg2, float4 diffuse)
{
    if (mode == 2) return arg1;
    if (mode == 3) return arg2;
    if (mode == 4 || mode == 17) return arg1 * arg2;
    if (mode == 5) return arg1 * arg2 * 2.0;
    if (mode == 6) return arg1 * arg2 * 4.0;
    if (mode == 7) return arg1 + arg2;
    if (mode == 8) return arg1 + arg2 - 0.5;
    if (mode == 9) return (arg1 + arg2 - 0.5) * 2.0;
    if (mode == 10) return arg1 - arg2;
    if (mode == 11) return arg1 + arg2 - arg1 * arg2;
    if (mode == 12) return lerp(arg2, arg1, diffuse.a);
    if (mode == 13) return lerp(arg2, arg1, arg1.a);
    if (mode == 14) return arg1;
    if (mode == 15) return arg1 + arg2 * (1.0 - arg1.a);
    if (mode == 16) return lerp(arg2, arg1, arg2.a);
    if (mode == 18) return float4(arg1.rgb + arg1.a * arg2.rgb, arg1.a * arg2.a);
    if (mode == 19) return float4(arg1.rgb * arg2.rgb + arg1.a, arg1.a * arg2.a);
    if (mode == 20) return float4(arg1.rgb + (1.0 - arg1.a) * arg2.rgb, arg1.a * arg2.a);
    if (mode == 21) return float4((1.0 - arg1.rgb) * arg2.rgb + arg1.a, arg1.a * arg2.a);
    if (mode == 22 || mode == 23) return arg2;
    if (mode == 24)
    {
        float product = dot(arg1.rgb * 2.0 - 1.0, arg2.rgb * 2.0 - 1.0);
        return float4(product, product, product, arg1.a * arg2.a);
    }
    if (mode == 25) return arg1 + arg2 * diffuse;
    if (mode == 26) return lerp(arg2, arg1, diffuse);
    return arg2;
}

float3 apply_fog(float3 result, float3 view_pos)
{
    if (flags.y > 0.5)
    {
        float dist = length(view_pos);
        float fog_start = dir_light_direction.w;
        float fog_range = fog_color_dist.a - fog_start;
        float fog_f = abs(fog_range) > 0.000001
            ? clamp((dist - fog_start) / fog_range, 0.0, 1.0)
            : step(fog_start, dist);
        result = lerp(result, srgb_to_linear(fog_color_dist.rgb), fog_f);
    }
    return result;
}

// Adds every light's diffuse and specular at a point, in view space.
void gather_lights(float3 N, float3 V, float3 view_pos, float spec_power, float fade, bool normalised_specular,
                   inout float3 lighting, inout float3 specular)
{
    // Directional light (affected by fade)
    if (dir_light_color.a > 0.0)
    {
        float3 L = normalize(-dir_light_direction.xyz);
        float3 light_color = srgb_to_linear(dir_light_color.rgb);
        lighting += diffuse_light(N, L, light_color) * fade;
        specular += (normalised_specular ? blinn_phong_light(N, L, V, light_color, spec_power)
                                         : specular_light(N, L, V, light_color, spec_power)) * fade;
    }

    // Point lights (affected by fade)
    int num_point = light_counts.x;
    for (int i = 0; i < num_point; ++i)
    {
        float4 pos_range = point_lights[i * 2];
        float4 color = point_lights[i * 2 + 1];

        float3 to_light = pos_range.xyz - view_pos;
        float dist = length(to_light);
        float range = pos_range.w;

        if (dist < range)
        {
            float3 L = to_light / max(dist, 0.0001);
            float atten = clamp(1.0 - dist / range, 0.0, 1.0);
            atten *= atten;
            float3 light_color = srgb_to_linear(color.rgb);
            lighting += diffuse_light(N, L, light_color) * atten * fade;
            specular += (normalised_specular ? blinn_phong_light(N, L, V, light_color, spec_power)
                                             : specular_light(N, L, V, light_color, spec_power)) * atten * fade;
        }
    }

    // Spot lights (affected by fade)
    int num_spot = light_counts.y;
    for (int j = 0; j < num_spot; ++j)
    {
        float4 pos_range = spot_lights[j * 3];
        float4 dir_inner = spot_lights[j * 3 + 1];
        float4 color_outer = spot_lights[j * 3 + 2];

        float3 to_light = pos_range.xyz - view_pos;
        float dist = length(to_light);
        float range = pos_range.w;

        if (dist < range)
        {
            float3 L = to_light / max(dist, 0.0001);
            float cos_angle = dot(-L, normalize(dir_inner.xyz));
            float cos_inner = dir_inner.w;
            float cos_outer = color_outer.a;

            float spot_f = clamp((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.0001), 0.0, 1.0);
            float atten = clamp(1.0 - dist / range, 0.0, 1.0);
            atten *= atten;

            float3 light_color = srgb_to_linear(color_outer.rgb);
            lighting += diffuse_light(N, L, light_color) * atten * spot_f * fade;
            specular += (normalised_specular ? blinn_phong_light(N, L, V, light_color, spec_power)
                                             : specular_light(N, L, V, light_color, spec_power)) *
                        atten * spot_f * fade;
        }
    }
}

// Undoes the sRGB decoding that sampling applied: a normal map's texels are data, not colour.
float3 srgb_encode(float3 c)
{
    const float3 low = c * 12.92;
    const float3 high = 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055;
    return float3(c.x <= 0.0031308 ? low.x : high.x, c.y <= 0.0031308 ? low.y : high.y,
                  c.z <= 0.0031308 ? low.z : high.z);
}

// Bends N by a tangent-space normal, with a frame from the screen derivatives of the position and the coordinates, so
// that meshes need no tangents. Green follows +V, the DirectX convention of the existing bump maps.
float3 perturb_normal(float3 N, float3 view_pos, float2 uv, float3 tangent_normal)
{
    const float3 dp1 = ddx(view_pos);
    const float3 dp2 = ddy(view_pos);
    const float2 duv1 = ddx(uv);
    const float2 duv2 = ddy(uv);
    const float3 dp2perp = cross(dp2, N);
    const float3 dp1perp = cross(N, dp1);
    const float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    const float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    const float size = max(dot(T, T), dot(B, B));
    if (size <= 1e-20)
        return N;
    // These are the gradients of U and V times the signed screen area, whichever way the axes turn.
    const float inverse = (dot(N, cross(dp1, dp2)) < 0.0 ? -1.0 : 1.0) / sqrt(size);
    return normalize(tangent_normal.x * T * inverse + tangent_normal.y * B * inverse + tangent_normal.z * N);
}

// Blinn-Phong with stage 1's normal map and a cube's reflection. N and V are normalised. The specular colour is the
// surface's reflectance head on, which Schlick's approximation raises at grazing angles as far as its gloss allows.
// Below 2%, which no real surface reflects, it shadows the grazing reflection too.
float4 shade_blinn_phong(Varyings input, float4 tex_color, float3 N, float3 V, float fade)
{
    if (material_params.x != 0.0)
    {
        const float3 tangent_normal = srgb_encode(tex1.Sample(sampler1, input.aux_uv0).rgb) * 2.0 - 1.0;
        N = perturb_normal(N, input.view_pos, input.aux_uv0, normalize(tangent_normal));
    }

    const float spec_power = max(input.box_br.a, 1.0);
    float3 spec_color = srgb_to_linear(input.box_br.rgb);
    // What a dielectric reflects, since a normal map shows little without a highlight.
    if (dot(spec_color, spec_color) <= 0.0)
        spec_color = 0.04;

    float3 lighting = srgb_to_linear(ambient_color.rgb) * ambient_color.a * srgb_to_linear(input.box_tr.rgb);
    float3 specular = 0.0;
    gather_lights(N, V, input.view_pos, spec_power, fade, true, lighting, specular);

    float3 result = tex_color.rgb * clamp(lighting, 0.0, max(2.0, 2.0 * fade));
    if (material_params.y > 0.0)
    {
        // From the sharpest level at the highest power that makes sense down to the broadest at power 1.
        const float gloss = clamp(log2(spec_power) / 11.0, 0.0, 1.0);
        const float grazing = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 5.0);
        const float grazing_limit = gloss * clamp(50.0 * max(spec_color.r, max(spec_color.g, spec_color.b)), 0.0, 1.0);
        const float3 reflectance = spec_color + max(grazing_limit - spec_color, 0.0) * grazing;
        const float3 view_reflection = reflect(-V, N);
        const float3 world_reflection =
            float3(dot(view_to_world[0].xyz, view_reflection), dot(view_to_world[1].xyz, view_reflection),
                   dot(view_to_world[2].xyz, view_reflection));
        const float level = (material_params.y - 1.0) * (1.0 - gloss);
        result = lerp(result, cube_tex.SampleLevel(cube_sampler, world_reflection, level).rgb, reflectance);
    }

    result += specular * spec_color;
    result += srgb_to_linear(input.box_bl.rgb);
    return float4(linear_to_srgb(apply_fog(result, input.view_pos)), tex_color.a);
}

float4 PSMain(Varyings input) : SV_Target0
{
    if (input.box_tr.a > 0.5)
    {
        float4 top = lerp(input.color, input.box_tr, input.uv.x);
        float4 bottom = lerp(input.box_bl, input.box_br, input.uv.x);
        return lerp(top, bottom, input.uv.y);
    }

    const float2 aux_uv[7] = {input.aux_uv0, input.aux_uv1, input.aux_uv2, input.aux_uv3,
                              input.aux_uv4, input.aux_uv5, input.aux_uv6};
    float4 diffuse = input.color;
    float4 base_sample = sample_2d_stage(0, input.uv);
    float4 tex_color = diffuse;
    int built_in_effect = stages[0].z;
    if (built_in_effect == 2)
    {
        tex_color = sample_2d_stage(0, input.uv) * sample_2d_stage(1, aux_uv[0]) * diffuse;
    }
    else if (built_in_effect == 3)
    {
        float4 rainbow0 = sample_2d_stage(0, input.uv);
        float4 rainbow1 = sample_2d_stage(1, aux_uv[0]);
        tex_color = float4(clamp(rainbow0.rgb * rainbow0.a * 2.0 + rainbow1.a - 0.5, 0.0, 1.0),
                           rainbow0.a * rainbow1.a * diffuse.a);
    }
    else
    {
        for (int stage = 0; stage < 8; ++stage)
        {
            int mode = stages[stage].x;
            if (mode == 0) continue;
            if (mode == 1) break;
            float2 stage_uv = stage == 0 ? input.uv : aux_uv[stage - 1];
            if (stage == 1 && material_params.z != 0.0)
                stage_uv *= material_params.zw;
            float4 stage_sample = stages[stage].y != 0
                ? cube_tex.Sample(cube_sampler, normalize(input.cube_direction))
                : sample_2d_stage(stage, stage_uv);
            if (mode == 27)
            {
                tex_color = float4(min(base_sample.rgb + diffuse.rgb, 1.0) * stage_sample.rgb,
                                   tex_color.a * stage_sample.a);
            }
            else
            {
                tex_color = clamp(apply_texture_operation(mode, stage_sample, tex_color, diffuse), 0.0, 1.0);
            }
        }
    }
    if (input.box_tr.a < -0.5)
    {
        tex_color.a = clamp(-input.box_tr.a - 1.0, 0.0, 1.0);
    }
    if (tex_color.a <= 0.001 || (input.box_bl.a > 0.0 && tex_color.a < input.box_bl.a))
    {
        discard;
    }

    if (built_in_effect == 2 || built_in_effect == 3)
    {
        return float4(linear_to_srgb(apply_fog(tex_color.rgb, input.view_pos)), tex_color.a);
    }

    float3 N = input.normal;
    float normal_mag = length(N);
    bool is_lit = normal_mag > 0.01;

    if (!is_lit)
    {
        // tex_color.rgb is already linear (hardware sRGB→linear on sample)
        return float4(linear_to_srgb(apply_fog(tex_color.rgb, input.view_pos)), tex_color.a);
    }

    // DBC FADE OBJECT: the normal magnitude encodes the fade multiplier.
    float fade = clamp(normal_mag, 0.0, 10.0);

    N = N / normal_mag;
    float3 V = normalize(-input.view_pos);
    if (built_in_effect == 4)
    {
        return shade_blinn_phong(input, tex_color, N, V, fade);
    }
    if (built_in_effect == 1)
    {
        float3 T = input.cube_direction - N * dot(input.cube_direction, N);
        if (length(T) < 0.0001)
        {
            T = cross(abs(N.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0), N);
        }
        T = normalize(T);
        float3 B = normalize(cross(N, T));
        float3 encoded_normal = linear_to_srgb(tex1.Sample(sampler1, input.uv).rgb) * 2.0 - 1.0;
        N = normalize(encoded_normal.x * T + encoded_normal.y * B + encoded_normal.z * N);
    }
    float spec_power = max(input.box_br.a, 1.0);

    // Albedo is already linear (hardware sRGB→linear on sample)
    float3 albedo = tex_color.rgb;

    // Ambient (NOT affected by fade — always applied)
    float3 lighting = srgb_to_linear(ambient_color.rgb) * ambient_color.a * srgb_to_linear(input.box_tr.rgb);
    float3 specular = 0.0;

    gather_lights(N, V, input.view_pos, spec_power, fade, false, lighting, specular);

    // Lighting applied in linear space (allow overbright for fade > 1)
    float3 result = albedo * clamp(lighting, 0.0, max(2.0, 2.0 * fade));
    result += specular * srgb_to_linear(input.box_br.rgb);
    result += srgb_to_linear(input.box_bl.rgb);

    // Fog blended in linear space, then converted back to sRGB for display
    return float4(linear_to_srgb(apply_fog(result, input.view_pos)), tex_color.a);
}
