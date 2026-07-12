#version 460 core

// VAO-less fullscreen triangle: (-1,-1), (3,-1), (-1,3).
out vec2 vNdc;
out vec2 vUv;

void main()
{
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vNdc = pos;
    vUv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
