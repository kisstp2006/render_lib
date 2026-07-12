#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uNormalMatrix;
uniform mat4 uCurrentViewProjection;
uniform mat4 uPreviousViewProjection;
uniform mat4 uPreviousModel;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vTangent; // xyz world-space tangent, w bitangent sign
out vec2 vUV;
out vec4 vCurrentClip;
out vec4 vPreviousClip;

void main()
{
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(uNormalMatrix) * aNormal);
    vTangent = vec4(normalize(mat3(uModel) * aTangent.xyz), aTangent.w);
    vUV = aUV;
    vCurrentClip = uCurrentViewProjection * worldPos;
    vPreviousClip = uPreviousViewProjection * uPreviousModel * vec4(aPosition, 1.0);
    gl_Position = vCurrentClip;
}
