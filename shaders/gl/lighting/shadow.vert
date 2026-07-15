#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec2 aUV;

#define ENGINE_OPENGL 1
#include "common/instance_data.glsl"

out vec2 vUV;

uniform mat4 uLightSpaceMatrix;

void main()
{
    vUV = aUV;
    gl_Position = uLightSpaceMatrix * ENGINE_INSTANCE_DATA.Model * vec4(aPosition, 1.0);
}
