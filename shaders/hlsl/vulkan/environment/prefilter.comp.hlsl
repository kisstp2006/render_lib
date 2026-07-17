#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer EnvironmentBakeConstants
{
    float4 pc_SunDirectionIntensity : packoffset(c0);
    float4 pc_SunColorAngularRadius : packoffset(c1);
    float4 pc_ZenithIntensity : packoffset(c2);
    float4 pc_HorizonCycle : packoffset(c3);
    float4 pc_GroundExposure : packoffset(c4);
    float4 pc_NightZenithIntensity : packoffset(c5);
    float4 pc_NightHorizonGlow : packoffset(c6);
    float4 pc_BakeParameters : packoffset(c7);
};

RWTexture2DArray<float4> outputCube : register(u0, space0);
TextureCube<float4> environmentMap : register(t1, space0);
SamplerState _environmentMap_sampler : register(s1, space0);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

uint3 spvImageSize(RWTexture2DArray<float4> Tex, out uint Param)
{
    uint3 ret;
    Tex.GetDimensions(ret.x, ret.y, ret.z);
    Param = 0u;
    return ret;
}

float3 CubeDirection(uint face, float2 uv)
{
    if (face == 0u)
    {
        return normalize(float3(1.0f, -uv.y, -uv.x));
    }
    if (face == 1u)
    {
        return normalize(float3(-1.0f, -uv.y, uv.x));
    }
    if (face == 2u)
    {
        return normalize(float3(uv.x, 1.0f, uv.y));
    }
    if (face == 3u)
    {
        return normalize(float3(uv.x, -1.0f, -uv.y));
    }
    if (face == 4u)
    {
        return normalize(float3(uv.x, -uv.y, 1.0f));
    }
    return normalize(float3(-uv.x, -uv.y, -1.0f));
}

float RadicalInverseVdc(inout uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 1431655765u) << 1u) | ((bits & 2863311530u) >> 1u);
    bits = ((bits & 858993459u) << 2u) | ((bits & 3435973836u) >> 2u);
    bits = ((bits & 252645135u) << 4u) | ((bits & 4042322160u) >> 4u);
    bits = ((bits & 16711935u) << 8u) | ((bits & 4278255360u) >> 8u);
    return float(bits) * 2.3283064365386962890625e-10f;
}

float2 Hammersley(uint i, uint count)
{
    uint param = i;
    float _173 = RadicalInverseVdc(param);
    return float2(float(i) / float(count), _173);
}

float3 ImportanceSampleGgx(float2 xi, float3 normal, float roughness)
{
    float a = roughness * roughness;
    float phi = 6.283185482025146484375f * xi.x;
    float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (((a * a) - 1.0f) * xi.y)));
    float sinTheta = sqrt(max(1.0f - (cosTheta * cosTheta), 0.0f));
    float3 halfVector = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    float3 up = (abs(normal.z) < 0.999000012874603271484375f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(((tangent * halfVector.x) + (bitangent * halfVector.y)) + (normal * halfVector.z));
}

float DistributionGgx(float nDotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (((nDotH * a2) - nDotH) * nDotH) + 1.0f;
    return a2 / max((3.1415927410125732421875f * d) * d, 1.0000000116860974230803549289703e-07f);
}

void comp_main()
{
    int3 coordinate = int3(gl_GlobalInvocationID);
    uint _296_dummy_parameter;
    int3 size = int3(spvImageSize(outputCube, _296_dummy_parameter));
    bool _302 = coordinate.x >= size.x;
    bool _311;
    if (!_302)
    {
        _311 = coordinate.y >= size.y;
    }
    else
    {
        _311 = _302;
    }
    bool _319;
    if (!_311)
    {
        _319 = coordinate.z >= 6;
    }
    else
    {
        _319 = _311;
    }
    if (_319)
    {
        return;
    }
    float2 uv = (((float2(coordinate.xy) + 0.5f.xx) / float2(size.xy)) * 2.0f) - 1.0f.xx;
    uint param = uint(coordinate.z);
    float2 param_1 = uv;
    float3 normal = CubeDirection(param, param_1);
    float3 viewDirection = normal;
    float roughness = pc_BakeParameters.x;
    uint sampleCount = uint(clamp(pc_BakeParameters.z, 1.0f, 1024.0f));
    float3 prefiltered = 0.0f.xxx;
    float totalWeight = 0.0f;
    float _451;
    for (uint i = 0u; i < sampleCount; i++)
    {
        uint param_2 = i;
        uint param_3 = sampleCount;
        float2 param_4 = Hammersley(param_2, param_3);
        float3 param_5 = normal;
        float param_6 = roughness;
        float3 halfVector = ImportanceSampleGgx(param_4, param_5, param_6);
        float3 lightDirection = normalize((halfVector * (2.0f * dot(viewDirection, halfVector))) - viewDirection);
        float nDotL = max(dot(normal, lightDirection), 0.0f);
        if (nDotL > 0.0f)
        {
            float nDotH = max(dot(normal, halfVector), 0.0f);
            float hDotV = max(dot(halfVector, viewDirection), 0.0f);
            float param_7 = nDotH;
            float param_8 = roughness;
            float pdf = ((DistributionGgx(param_7, param_8) * nDotH) / (4.0f * hDotV)) + 9.9999997473787516355514526367188e-05f;
            float texelSolidAngle = 12.56637096405029296875f / ((6.0f * pc_BakeParameters.y) * pc_BakeParameters.y);
            float sampleSolidAngle = 1.0f / ((float(sampleCount) * pdf) + 9.9999997473787516355514526367188e-05f);
            if (roughness == 0.0f)
            {
                _451 = 0.0f;
            }
            else
            {
                _451 = 0.5f * log2(sampleSolidAngle / texelSolidAngle);
            }
            float mip = _451;
            prefiltered += (environmentMap.SampleLevel(_environmentMap_sampler, lightDirection, mip).xyz * nDotL);
            totalWeight += nDotL;
        }
    }
    outputCube[coordinate] = float4(prefiltered / max(totalWeight, 9.9999997473787516355514526367188e-05f).xxx, 1.0f);
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
