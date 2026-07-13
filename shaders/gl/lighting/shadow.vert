#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec2 aUV;

out vec2 vUV;

uniform mat4 uModel;
uniform mat4 uLightSpaceMatrix;

void main()
{
    vUV = aUV;
    gl_Position = uLightSpaceMatrix * uModel * vec4(aPosition, 1.0);
}
