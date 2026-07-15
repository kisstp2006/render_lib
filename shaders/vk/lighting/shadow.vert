#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 inUv;

layout(location = 0) out vec2 uv;

#define ENGINE_VULKAN 1
#include "common/instance_data.glsl"

layout(set = 0, binding = 0, std140) uniform ShadowUniforms
{
    mat4 LightViewProjection;
} shadow;

void main()
{
    uv = inUv;
    gl_Position = shadow.LightViewProjection * ENGINE_INSTANCE_DATA.Model
                * vec4(inPosition, 1.0);
}
