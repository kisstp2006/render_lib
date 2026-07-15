#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;

#define ENGINE_OPENGL 1
#include "common/instance_data.glsl"

uniform mat4 uCurrentViewProjection;
uniform mat4 uPreviousViewProjection;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vTangent; // xyz world-space tangent, w bitangent sign
out vec2 vUV;
out vec4 vCurrentClip;
out vec4 vPreviousClip;

void main()
{
    mat4 model = ENGINE_INSTANCE_DATA.Model;
    mat4 previousModel = ENGINE_INSTANCE_DATA.PreviousModel;
    vec4 worldPos = model * vec4(aPosition, 1.0);
    vec3 worldNormal = normalize(transpose(inverse(mat3(model))) * aNormal);
    vec3 worldTangent = normalize(mat3(model) * aTangent.xyz);
    worldTangent = normalize(worldTangent - worldNormal * dot(worldNormal, worldTangent));
    vWorldPos = worldPos.xyz;
    vNormal = worldNormal;
    vTangent = vec4(worldTangent, aTangent.w);
    vUV = aUV;
    vCurrentClip = uCurrentViewProjection * worldPos;
    vPreviousClip = uPreviousViewProjection * previousModel * vec4(aPosition, 1.0);
    gl_Position = vCurrentClip;
}
