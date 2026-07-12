#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vTangent; // xyz world-space tangent, w bitangent sign
out vec2 vUV;

void main()
{
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(uNormalMatrix) * aNormal);
    vTangent = vec4(normalize(mat3(uModel) * aTangent.xyz), aTangent.w);
    vUV = aUV;
    gl_Position = uProj * uView * worldPos;
}
