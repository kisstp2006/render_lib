#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 inUv;
layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec2 uv;

layout(set = 0, binding = 0, std140) uniform ShadowUniforms
{
    mat4 LightViewProjection;
    vec4 LightPositionRange;
} shadow;

layout(push_constant) uniform ObjectConstants
{
    mat4 Model;
} objectData;

void main()
{
    vec4 world = objectData.Model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    uv = inUv;
    gl_Position = shadow.LightViewProjection * world;
}
