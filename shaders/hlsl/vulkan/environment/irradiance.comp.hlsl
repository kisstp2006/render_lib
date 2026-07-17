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

void comp_main()
{
    int3 coordinate = int3(gl_GlobalInvocationID);
    uint _110_dummy_parameter;
    int3 size = int3(spvImageSize(outputCube, _110_dummy_parameter));
    bool _116 = coordinate.x >= size.x;
    bool _125;
    if (!_116)
    {
        _125 = coordinate.y >= size.y;
    }
    else
    {
        _125 = _116;
    }
    bool _133;
    if (!_125)
    {
        _133 = coordinate.z >= 6;
    }
    else
    {
        _133 = _125;
    }
    if (_133)
    {
        return;
    }
    float2 uv = (((float2(coordinate.xy) + 0.5f.xx) / float2(size.xy)) * 2.0f) - 1.0f.xx;
    uint param = uint(coordinate.z);
    float2 param_1 = uv;
    float3 normal = CubeDirection(param, param_1);
    float3 up = (abs(normal.y) < 0.999000012874603271484375f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));
    float3 irradiance = 0.0f.xxx;
    int sampleCount = 0;
    float delta = max(pc_BakeParameters.z, 0.02500000037252902984619140625f);
    for (float phi = 0.0f; phi < 6.283185482025146484375f; phi += delta)
    {
        for (float theta = 0.0f; theta < 1.57079637050628662109375f; theta += delta)
        {
            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 sampleDirection = ((right * tangentSample.x) + (up * tangentSample.y)) + (normal * tangentSample.z);
            irradiance += ((environmentMap.SampleLevel(_environmentMap_sampler, sampleDirection, 3.0f).xyz * cos(theta)) * sin(theta));
            sampleCount++;
        }
    }
    outputCube[coordinate] = float4((irradiance * 3.1415927410125732421875f) / float(sampleCount).xxx, 1.0f);
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
