#version 460 core

// GGX-prefiltered specular environment map (Karis split-sum, first half).
// Mip level <-> roughness, sampled in pbr.frag via textureLod like VRF's
// GetEnvMapLOD (roughness * mip count).

in vec2 vNdc;
out vec4 FragColor;

uniform mat3 uFaceBasis;
uniform samplerCube uEnvMap;
uniform float uRoughness;
uniform float uEnvResolution;
uniform int uSampleCount;

const float PI = 3.14159265359;
const uint MAX_SAMPLE_COUNT = 1024u;

float DistributionGGX(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint n)
{
    return vec2(float(i) / float(n), RadicalInverse_VdC(i));
}

vec3 ImportanceSampleGGX(vec2 xi, vec3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

void main()
{
    vec3 N = normalize(uFaceBasis * vec3(vNdc, 1.0));
    vec3 R = N;
    vec3 V = R;

    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    uint sampleCount = uint(clamp(uSampleCount, 1, int(MAX_SAMPLE_COUNT)));
    for (uint i = 0u; i < sampleCount; ++i)
    {
        vec2 xi = Hammersley(i, sampleCount);
        vec3 H = ImportanceSampleGGX(xi, N, uRoughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0)
        {
            // PDF-matched mip selection keeps the tiny bright sun from
            // producing fireflies in rough mips.
            float NoH = max(dot(N, H), 0.0);
            float HoV = max(dot(H, V), 0.0);
            float D = DistributionGGX(NoH, uRoughness);
            float pdf = D * NoH / (4.0 * HoV) + 0.0001;

            float saTexel = 4.0 * PI / (6.0 * uEnvResolution * uEnvResolution);
            float saSample = 1.0 / (float(sampleCount) * pdf + 0.0001);
            float mipLevel = uRoughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

            prefiltered += textureLod(uEnvMap, L, mipLevel).rgb * NoL;
            totalWeight += NoL;
        }
    }

    FragColor = vec4(prefiltered / max(totalWeight, 1e-4), 1.0);
}
