#version 460 core

in vec2 vNdc;
out vec4 FragColor;

uniform mat3 uFaceBasis;
uniform sampler2D uEquirectangularMap;
uniform float uRotation;
uniform float uIntensity;

const float PI = 3.14159265358979323846;

void main()
{
    vec3 direction = normalize(uFaceBasis * vec3(vNdc, 1.0));
    float longitude = atan(direction.z, direction.x) + uRotation;
    vec2 uv = vec2(fract(longitude / (2.0 * PI) + 0.5),
                   acos(clamp(direction.y, -1.0, 1.0)) / PI);
    vec3 radiance = max(texture(uEquirectangularMap, uv).rgb, vec3(0.0));
    FragColor = vec4(radiance * uIntensity, 1.0);
}
