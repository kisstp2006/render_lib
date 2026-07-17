cbuffer EngineGlobals_environment_prefilter_frag_hlsl : register(b13)
{
    row_major float3x3 uFaceBasis;
    int uSampleCount;
    float uRoughness;
    float uEnvResolution;
};

TextureCube<float4> uEnvMap : register(t0);
SamplerState _uEnvMap_sampler : register(s0);

static float2 vNdc;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float2 vNdc : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float RadicalInverse_VdC(inout uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 1431655765u) << 1u) | ((bits & 2863311530u) >> 1u);
    bits = ((bits & 858993459u) << 2u) | ((bits & 3435973836u) >> 2u);
    bits = ((bits & 252645135u) << 4u) | ((bits & 4042322160u) >> 4u);
    bits = ((bits & 16711935u) << 8u) | ((bits & 4278255360u) >> 8u);
    return float(bits) * 2.3283064365386962890625e-10f;
}

float2 Hammersley(uint i, uint n)
{
    uint param = i;
    float _122 = RadicalInverse_VdC(param);
    return float2(float(i) / float(n), _122);
}

float3 ImportanceSampleGGX(float2 xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 6.283185482025146484375f * xi.x;
    float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (((a * a) - 1.0f) * xi.y)));
    float sinTheta = sqrt(1.0f - (cosTheta * cosTheta));
    float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    bool3 _178 = (abs(N.z) < 0.999000012874603271484375f).xxx;
    float3 up = float3(_178.x ? float3(0.0f, 0.0f, 1.0f).x : float3(1.0f, 0.0f, 0.0f).x, _178.y ? float3(0.0f, 0.0f, 1.0f).y : float3(1.0f, 0.0f, 0.0f).y, _178.z ? float3(0.0f, 0.0f, 1.0f).z : float3(1.0f, 0.0f, 0.0f).z);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(((tangent * H.x) + (bitangent * H.y)) + (N * H.z));
}

float DistributionGGX(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (((NoH * a2) - NoH) * NoH) + 1.0f;
    return a2 / max((3.1415927410125732421875f * d) * d, 1.0000000116860974230803549289703e-07f);
}

void frag_main()
{
    float3 N = normalize(mul(float3(vNdc, 1.0f), uFaceBasis));
    float3 R = N;
    float3 V = R;
    float3 prefiltered = 0.0f.xxx;
    float totalWeight = 0.0f;
    uint sampleCount = uint(clamp(uSampleCount, 1, 1024));
    float _325;
    for (uint i = 0u; i < sampleCount; i++)
    {
        uint param = i;
        uint param_1 = sampleCount;
        float2 xi = Hammersley(param, param_1);
        float2 param_2 = xi;
        float3 param_3 = N;
        float param_4 = uRoughness;
        float3 H = ImportanceSampleGGX(param_2, param_3, param_4);
        float3 L = normalize((H * (2.0f * dot(V, H))) - V);
        float NoL = max(dot(N, L), 0.0f);
        if (NoL > 0.0f)
        {
            float NoH = max(dot(N, H), 0.0f);
            float HoV = max(dot(H, V), 0.0f);
            float param_5 = NoH;
            float param_6 = uRoughness;
            float D = DistributionGGX(param_5, param_6);
            float pdf = ((D * NoH) / (4.0f * HoV)) + 9.9999997473787516355514526367188e-05f;
            float saTexel = 12.56637096405029296875f / ((6.0f * uEnvResolution) * uEnvResolution);
            float saSample = 1.0f / ((float(sampleCount) * pdf) + 9.9999997473787516355514526367188e-05f);
            if (uRoughness == 0.0f)
            {
                _325 = 0.0f;
            }
            else
            {
                _325 = 0.5f * log2(saSample / saTexel);
            }
            float mipLevel = _325;
            prefiltered += (uEnvMap.SampleLevel(_uEnvMap_sampler, L, mipLevel).xyz * NoL);
            totalWeight += NoL;
        }
    }
    FragColor = float4(prefiltered / max(totalWeight, 9.9999997473787516355514526367188e-05f).xxx, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vNdc = stage_input.vNdc;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
