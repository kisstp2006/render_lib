#version 460

layout(push_constant) uniform BoundsDebugConstants
{
    vec4 Minimum;
    vec4 Maximum;
    vec4 Color;
} boundsData;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

void main()
{
    outColor = vec4(boundsData.Color.rgb, 1.0);
    outVelocity = vec2(0.0);
}
