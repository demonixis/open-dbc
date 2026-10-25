#version 450

layout(location = 0) in vec3 in_view_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;
layout(location = 4) in vec4 in_box_tr;
layout(location = 5) in vec4 in_box_br;
layout(location = 6) in vec4 in_box_bl;
layout(location = 7) in vec2 in_aux_uv[7];
layout(location = 14) in vec3 in_cube_direction;

layout(location = 0) out vec4 out_frag;

layout(set = 2, binding = 0) uniform sampler2D texSampler0;
layout(set = 2, binding = 1) uniform sampler2D texSampler1;
layout(set = 2, binding = 2) uniform sampler2D texSampler2;
layout(set = 2, binding = 3) uniform sampler2D texSampler3;
layout(set = 2, binding = 4) uniform sampler2D texSampler4;
layout(set = 2, binding = 5) uniform sampler2D texSampler5;
layout(set = 2, binding = 6) uniform sampler2D texSampler6;
layout(set = 2, binding = 7) uniform sampler2D texSampler7;
layout(set = 2, binding = 8) uniform samplerCube cubeSampler;

layout(set = 3, binding = 0, std140) uniform FragmentUniforms {
    vec4 dir_light_direction;
    vec4 dir_light_color;
    vec4 ambient_color;
    vec4 fog_color_dist;
    vec4 flags;
    ivec4 light_counts;
    vec4 point_lights[24];
    vec4 spot_lights[18];
} frag;

layout(set = 3, binding = 1, std140) uniform TextureStageUniforms {
    ivec4 stages[8];
} texture_stages;

// Gamma correction for uniform colors (light colors, fog, ambient).
// Texture samples are already linear — the GPU converts sRGB→linear
// automatically when using UNORM_SRGB texture format.

vec3 srgb_to_linear(vec3 c) {
    return pow(max(c, 0.0), vec3(2.2));
}

vec3 linear_to_srgb(vec3 c) {
    return pow(max(c, 0.0), vec3(1.0 / 2.2));
}

vec3 diffuse_light(vec3 N, vec3 L, vec3 light_color) {
    float diff = max(dot(N, L), 0.0);
    return light_color * diff;
}

vec3 specular_light(vec3 N, vec3 L, vec3 V, vec3 light_color, float spec_power) {
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), spec_power);
    return light_color * spec * 0.15;
}

vec4 sample_2d_stage(int stage, vec2 uv) {
    if (stage == 0) return texture(texSampler0, uv);
    if (stage == 1) return texture(texSampler1, uv);
    if (stage == 2) return texture(texSampler2, uv);
    if (stage == 3) return texture(texSampler3, uv);
    if (stage == 4) return texture(texSampler4, uv);
    if (stage == 5) return texture(texSampler5, uv);
    if (stage == 6) return texture(texSampler6, uv);
    return texture(texSampler7, uv);
}

vec4 apply_texture_operation(int mode, vec4 arg1, vec4 arg2, vec4 diffuse) {
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
    if (mode == 12) return mix(arg2, arg1, diffuse.a);
    if (mode == 13) return mix(arg2, arg1, arg1.a);
    if (mode == 14) return arg1;
    if (mode == 15) return arg1 + arg2 * (1.0 - arg1.a);
    if (mode == 16) return mix(arg2, arg1, arg2.a);
    if (mode == 18) return vec4(arg1.rgb + arg1.a * arg2.rgb, arg1.a * arg2.a);
    if (mode == 19) return vec4(arg1.rgb * arg2.rgb + vec3(arg1.a), arg1.a * arg2.a);
    if (mode == 20) return vec4(arg1.rgb + (1.0 - arg1.a) * arg2.rgb, arg1.a * arg2.a);
    if (mode == 21) return vec4((1.0 - arg1.rgb) * arg2.rgb + vec3(arg1.a), arg1.a * arg2.a);
    if (mode == 22 || mode == 23) return arg2;
    if (mode == 24) {
        float product = dot(arg1.rgb * 2.0 - 1.0, arg2.rgb * 2.0 - 1.0);
        return vec4(vec3(product), arg1.a * arg2.a);
    }
    if (mode == 25) return arg1 + arg2 * diffuse;
    if (mode == 26) return mix(arg2, arg1, diffuse);
    return arg2;
}

void main() {
    if (in_box_tr.a > 0.5) {
        vec4 top = mix(in_color, in_box_tr, in_uv.x);
        vec4 bottom = mix(in_box_bl, in_box_br, in_uv.x);
        out_frag = mix(top, bottom, in_uv.y);
        return;
    }

    vec4 diffuse = in_color;
    vec4 base_sample = sample_2d_stage(0, in_uv);
    vec4 tex_color = diffuse;
    int built_in_effect = texture_stages.stages[0].z;
    if (built_in_effect == 2) {
        tex_color = sample_2d_stage(0, in_uv) * sample_2d_stage(1, in_aux_uv[0]) * diffuse;
    } else if (built_in_effect == 3) {
        vec4 rainbow0 = sample_2d_stage(0, in_uv);
        vec4 rainbow1 = sample_2d_stage(1, in_aux_uv[0]);
        tex_color = vec4(clamp(rainbow0.rgb * rainbow0.a * 2.0 + vec3(rainbow1.a) - 0.5, 0.0, 1.0),
                         rainbow0.a * rainbow1.a * diffuse.a);
    } else {
        for (int stage = 0; stage < 8; ++stage) {
            int mode = texture_stages.stages[stage].x;
            if (mode == 0) continue;
            if (mode == 1) break;
            vec2 stage_uv = stage == 0 ? in_uv : in_aux_uv[stage - 1];
            vec4 stage_sample = texture_stages.stages[stage].y != 0
                ? texture(cubeSampler, normalize(in_cube_direction))
                : sample_2d_stage(stage, stage_uv);
            if (mode == 27) {
                tex_color = vec4(min(base_sample.rgb + diffuse.rgb, vec3(1.0)) * stage_sample.rgb,
                                 tex_color.a * stage_sample.a);
            } else {
                tex_color = clamp(apply_texture_operation(mode, stage_sample, tex_color, diffuse), 0.0, 1.0);
            }
        }
    }
    if (in_box_tr.a < -0.5) {
        tex_color.a = clamp(-in_box_tr.a - 1.0, 0.0, 1.0);
    }
    if (tex_color.a <= 0.001 || (in_box_bl.a > 0.0 && tex_color.a < in_box_bl.a)) {
        discard;
    }

    if (built_in_effect == 2 || built_in_effect == 3) {
        vec3 result = tex_color.rgb;
        if (frag.flags.y > 0.5) {
            float dist = length(in_view_pos);
            float fog_start = frag.dir_light_direction.w;
            float fog_range = frag.fog_color_dist.a - fog_start;
            float fog_f = abs(fog_range) > 0.000001
                ? clamp((dist - fog_start) / fog_range, 0.0, 1.0)
                : step(fog_start, dist);
            result = mix(result, srgb_to_linear(frag.fog_color_dist.rgb), fog_f);
        }
        out_frag = vec4(linear_to_srgb(result), tex_color.a);
        return;
    }

    vec3 N = in_normal;
    float normal_mag = length(N);
    bool is_lit = normal_mag > 0.01;

    if (!is_lit) {
        // tex_color.rgb is already linear (hardware sRGB→linear on sample)
        vec3 result = tex_color.rgb;
        if (frag.flags.y > 0.5) {
            float dist = length(in_view_pos);
            float fog_start = frag.dir_light_direction.w;
            float fog_range = frag.fog_color_dist.a - fog_start;
            float fog_f = abs(fog_range) > 0.000001
                ? clamp((dist - fog_start) / fog_range, 0.0, 1.0)
                : step(fog_start, dist);
            result = mix(result, srgb_to_linear(frag.fog_color_dist.rgb), fog_f);
        }
        out_frag = vec4(linear_to_srgb(result), tex_color.a);
        return;
    }

    // DBC FADE OBJECT: the normal magnitude encodes the fade multiplier.
    float fade = clamp(normal_mag, 0.0, 10.0);

    N = N / normal_mag;
    vec3 V = normalize(-in_view_pos);
    if (built_in_effect == 1) {
        vec3 T = in_cube_direction - N * dot(in_cube_direction, N);
        if (length(T) < 0.0001) {
            T = cross(abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), N);
        }
        T = normalize(T);
        vec3 B = normalize(cross(N, T));
        vec3 encoded_normal = linear_to_srgb(texture(texSampler1, in_uv).rgb) * 2.0 - 1.0;
        N = normalize(encoded_normal.x * T + encoded_normal.y * B + encoded_normal.z * N);
    }
    float spec_power = max(in_box_br.a, 1.0);

    // Albedo is already linear (hardware sRGB→linear on sample)
    vec3 albedo = tex_color.rgb;

    // Ambient (NOT affected by fade — always applied)
    vec3 lighting = srgb_to_linear(frag.ambient_color.rgb) * frag.ambient_color.a *
                    srgb_to_linear(in_box_tr.rgb);
    vec3 specular = vec3(0.0);

    // Directional light (affected by fade)
    if (frag.dir_light_color.a > 0.0) {
        vec3 L = normalize(-frag.dir_light_direction.xyz);
        vec3 light_color = srgb_to_linear(frag.dir_light_color.rgb);
        lighting += diffuse_light(N, L, light_color) * fade;
        specular += specular_light(N, L, V, light_color, spec_power) * fade;
    }

    // Point lights (affected by fade)
    int num_point = frag.light_counts.x;
    for (int i = 0; i < num_point; ++i) {
        vec4 pos_range = frag.point_lights[i * 2];
        vec4 color = frag.point_lights[i * 2 + 1];

        vec3 to_light = pos_range.xyz - in_view_pos;
        float dist = length(to_light);
        float range = pos_range.w;

        if (dist < range) {
            vec3 L = to_light / max(dist, 0.0001);
            float atten = clamp(1.0 - dist / range, 0.0, 1.0);
            atten *= atten;
            vec3 light_color = srgb_to_linear(color.rgb);
            lighting += diffuse_light(N, L, light_color) * atten * fade;
            specular += specular_light(N, L, V, light_color, spec_power) * atten * fade;
        }
    }

    // Spot lights (affected by fade)
    int num_spot = frag.light_counts.y;
    for (int i = 0; i < num_spot; ++i) {
        vec4 pos_range = frag.spot_lights[i * 3];
        vec4 dir_inner = frag.spot_lights[i * 3 + 1];
        vec4 color_outer = frag.spot_lights[i * 3 + 2];

        vec3 to_light = pos_range.xyz - in_view_pos;
        float dist = length(to_light);
        float range = pos_range.w;

        if (dist < range) {
            vec3 L = to_light / max(dist, 0.0001);
            float cos_angle = dot(-L, normalize(dir_inner.xyz));
            float cos_inner = dir_inner.w;
            float cos_outer = color_outer.a;

            float spot_f = clamp((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.0001), 0.0, 1.0);
            float atten = clamp(1.0 - dist / range, 0.0, 1.0);
            atten *= atten;

            vec3 light_color = srgb_to_linear(color_outer.rgb);
            lighting += diffuse_light(N, L, light_color) * atten * spot_f * fade;
            specular += specular_light(N, L, V, light_color, spec_power) * atten * spot_f * fade;
        }
    }

    // Lighting applied in linear space (allow overbright for fade > 1)
    vec3 result = albedo * clamp(lighting, 0.0, max(2.0, 2.0 * fade));
    result += specular * srgb_to_linear(in_box_br.rgb);
    result += srgb_to_linear(in_box_bl.rgb);

    // Fog blended in linear space
    if (frag.flags.y > 0.5) {
        float dist = length(in_view_pos);
        float fog_start = frag.dir_light_direction.w;
        float fog_range = frag.fog_color_dist.a - fog_start;
        float fog_f = abs(fog_range) > 0.000001
            ? clamp((dist - fog_start) / fog_range, 0.0, 1.0)
            : step(fog_start, dist);
        result = mix(result, srgb_to_linear(frag.fog_color_dist.rgb), fog_f);
    }

    // Convert back to sRGB for display
    out_frag = vec4(linear_to_srgb(result), tex_color.a);
}
