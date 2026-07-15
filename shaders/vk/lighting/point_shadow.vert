#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 inUv;
layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec2 uv;

#define ENGINE_VULKAN 1
#include "common/instance_data.glsl"

layout(set = 0, binding = 0, std140) uniform ShadowUniforms
{
    mat4 LightViewProjection;
    vec4 LightPositionRange;
} shadow;

void main()
{
    vec4 world = ENGINE_INSTANCE_DATA.Model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    uv = inUv;
    gl_Position = shadow.LightViewProjection * world;
}
