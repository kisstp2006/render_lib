#version 460

#include "common/frame_uniforms.glsl"

layout(push_constant) uniform BoundsDebugConstants
{
    vec4 Minimum;
    vec4 Maximum;
    vec4 Color;
} boundsData;

const vec3 corners[24] = vec3[](
    vec3(0,0,0), vec3(1,0,0), vec3(1,0,0), vec3(1,1,0),
    vec3(1,1,0), vec3(0,1,0), vec3(0,1,0), vec3(0,0,0),
    vec3(0,0,1), vec3(1,0,1), vec3(1,0,1), vec3(1,1,1),
    vec3(1,1,1), vec3(0,1,1), vec3(0,1,1), vec3(0,0,1),
    vec3(0,0,0), vec3(0,0,1), vec3(1,0,0), vec3(1,0,1),
    vec3(1,1,0), vec3(1,1,1), vec3(0,1,0), vec3(0,1,1));

void main()
{
    vec3 worldPosition = boundsData.Color.w < 0.0
        ? (gl_VertexIndex == 0 ? boundsData.Minimum.xyz : boundsData.Maximum.xyz)
        : mix(boundsData.Minimum.xyz, boundsData.Maximum.xyz,
              corners[gl_VertexIndex]);
    gl_Position = frame.Projection * frame.View * vec4(worldPosition, 1.0);
}
