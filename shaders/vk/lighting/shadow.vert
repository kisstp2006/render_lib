#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 inUv;

layout(location = 0) out vec2 uv;

layout(set = 0, binding = 0, std140) uniform ShadowUniforms
{
    mat4 LightViewProjection;
} shadow;

layout(push_constant) uniform ObjectConstants
{
    mat4 Model;
} objectData;

void main()
{
    uv = inUv;
    gl_Position = shadow.LightViewProjection * objectData.Model * vec4(inPosition, 1.0);
}
