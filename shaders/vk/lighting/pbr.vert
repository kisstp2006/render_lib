#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUv;

#include "common/frame_uniforms.glsl"

layout(push_constant) uniform ObjectConstants
{
    mat4 Model;
    mat4 PreviousModel;
} objectData;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec4 worldTangent;
layout(location = 3) out vec2 uv;
layout(location = 4) out vec4 currentClip;
layout(location = 5) out vec4 previousClip;

void main()
{
    vec4 world = objectData.Model * vec4(inPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(objectData.Model)));
    worldPosition = world.xyz;
    worldNormal = normalize(normalMatrix * inNormal);
    worldTangent = vec4(normalize(normalMatrix * inTangent.xyz), inTangent.w);
    uv = inUv;
    currentClip = frame.Projection * frame.View * world;
    previousClip = frame.PreviousViewProjection * objectData.PreviousModel
                 * vec4(inPosition, 1.0);
    gl_Position = currentClip;
}
