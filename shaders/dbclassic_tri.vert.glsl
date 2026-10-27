#version 450

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in vec4 in_box_tr;
layout(location = 5) in vec4 in_box_br;
layout(location = 6) in vec4 in_box_bl;
layout(location = 7) in vec2 in_aux_uv[7];
layout(location = 14) in vec3 in_cube_direction;

layout(location = 0) out vec3 out_view_pos;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) out vec4 out_color;
layout(location = 4) out vec4 out_box_tr;
layout(location = 5) out vec4 out_box_br;
layout(location = 6) out vec4 out_box_bl;
layout(location = 7) out vec2 out_aux_uv[7];
layout(location = 14) out vec3 out_cube_direction;

layout(set = 1, binding = 0, std140) uniform VertexUniforms {
    mat4 ViewProj;
} ubo;

// Identity for geometry already in view space; the world-to-view matrix for static world-space meshes.
layout(set = 1, binding = 1, std140) uniform ModelUniforms {
    mat4 ModelView;
} model;

void main() {
    vec4 view_pos = model.ModelView * vec4(in_pos, 1.0);
    gl_Position = ubo.ViewProj * view_pos;
    out_view_pos = view_pos.xyz;
    out_normal = mat3(model.ModelView) * in_normal;
    out_uv = in_uv;
    out_color = in_color;
    out_box_tr = in_box_tr;
    out_box_br = in_box_br;
    out_box_bl = in_box_bl;
    for (int stage = 0; stage < 7; ++stage) {
        out_aux_uv[stage] = in_aux_uv[stage];
    }
    out_cube_direction = in_cube_direction;
}
