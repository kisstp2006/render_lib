#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUv;

#include "common/frame_uniforms.glsl"

#define ENGINE_VULKAN 1
#include "common/instance_data.glsl"

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec4 worldTangent;
layout(location = 3) out vec2 uv;
layout(location = 4) out vec4 currentClip;
layout(location = 5) out vec4 previousClip;

void main()
{
    mat4 model = ENGINE_INSTANCE_DATA.Model;
    mat4 previousModel = ENGINE_INSTANCE_DATA.PreviousModel;
    vec4 world = model * vec4(inPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 transformedNormal = normalize(normalMatrix * inNormal);
    vec3 transformedTangent = normalize(mat3(model) * inTangent.xyz);
    transformedTangent = normalize(transformedTangent
                                 - transformedNormal * dot(transformedNormal, transformedTangent));
    worldPosition = world.xyz;
    worldNormal = transformedNormal;
    worldTangent = vec4(transformedTangent, inTangent.w);
    uv = inUv;
    currentClip = frame.Projection * frame.View * world;
    previousClip = frame.PreviousViewProjection * previousModel
                 * vec4(inPosition, 1.0);
    gl_Position = currentClip;
}
