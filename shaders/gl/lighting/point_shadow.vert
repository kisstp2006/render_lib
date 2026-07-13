#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uFaceMatrix;

out vec3 vWorldPos;
out vec2 vUV;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vUV = aUV;
    gl_Position = uFaceMatrix * world;
}
