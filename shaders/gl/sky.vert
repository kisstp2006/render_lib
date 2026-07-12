#version 460 core

// Fullscreen triangle pinned to the far plane (z = w -> depth 1.0), drawn
// after opaque geometry with GL_LEQUAL so only empty pixels get sky.
out vec2 vNdc;

void main()
{
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vNdc = pos;
    gl_Position = vec4(pos, 1.0, 1.0);
}
