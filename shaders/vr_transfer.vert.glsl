#version 450
layout(location = 0) out vec2 Uv;
void main()
{
    Uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(Uv.x * 2.0 - 1.0, 1.0 - Uv.y * 2.0, 0.0, 1.0);
}
