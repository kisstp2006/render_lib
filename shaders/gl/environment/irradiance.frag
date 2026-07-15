#version 460 core

// Cosine-weighted hemisphere convolution of the environment cubemap into a
// small irradiance cubemap - the diffuse half of the IBL split.

in vec2 vNdc;
out vec4 FragColor;

uniform mat3 uFaceBasis;
uniform samplerCube uEnvMap;
uniform float uSampleDelta;

const float PI = 3.14159265359;

void main()
{
    vec3 N = normalize(uFaceBasis * vec3(vNdc, 1.0));

    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    vec3 irradiance = vec3(0.0);
    int sampleCount = 0;

    float delta = max(uSampleDelta, 0.025);
    for (float phi = 0.0; phi < 2.0 * PI; phi += delta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += delta)
        {
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleDir = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            // Sample a blurred mip so the tiny ultra-bright sun disk doesn't
            // alias into splotches; energy is preserved well enough.
            irradiance += textureLod(uEnvMap, sampleDir, 3.0).rgb * cos(theta) * sin(theta);
            ++sampleCount;
        }
    }

    irradiance = PI * irradiance / float(sampleCount);
    FragColor = vec4(irradiance, 1.0);
}
