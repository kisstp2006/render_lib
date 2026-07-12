#version 460 core

in vec2 vNdc;
out vec4 FragColor;

uniform samplerCube uEnvMap;
uniform mat4 uInvProj;
uniform mat4 uInvView;

void main()
{
    vec4 viewRay = uInvProj * vec4(vNdc, 1.0, 1.0);
    vec3 dirView = normalize(viewRay.xyz / viewRay.w);
    vec3 dirWorld = normalize(mat3(uInvView) * dirView);

    FragColor = vec4(textureLod(uEnvMap, dirWorld, 0.0).rgb, 1.0);
}
