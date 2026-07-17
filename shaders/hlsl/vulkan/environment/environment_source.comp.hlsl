struct EngineProceduralSkyParameters
{
    float3 ZenithColor;
    float3 HorizonColor;
    float3 GroundColor;
    float3 NightZenithColor;
    float3 NightHorizonColor;
    float3 SunColor;
    float SkyIntensity;
    float NightSkyIntensity;
    float NightHorizonGlow;
    bool EnableDayNightCycle;
};

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
Texture2D<float4> panorama : register(t1, space0);
SamplerState _panorama_sampler : register(s1, space0);

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

float3 EngineEvaluateProceduralSky(float3 direction, float3 toSun, EngineProceduralSkyParameters parameters)
{
    float h = direction.y;
    float3 daySky = lerp(parameters.HorizonColor, parameters.ZenithColor, pow(clamp(h, 0.0f, 1.0f), 0.550000011920928955078125f).xxx);
    float3 dayGround = lerp(parameters.HorizonColor * 0.85000002384185791015625f, parameters.GroundColor, pow(clamp(-h, 0.0f, 1.0f), 0.4000000059604644775390625f).xxx);
    float3 nightSky = lerp(parameters.NightHorizonColor * parameters.NightHorizonGlow, parameters.NightZenithColor, pow(clamp(h, 0.0f, 1.0f), 0.449999988079071044921875f).xxx) * parameters.NightSkyIntensity;
    float3 nightGround = parameters.NightZenithColor * (0.119999997317790985107421875f * parameters.NightSkyIntensity);
    float sunElevation = degrees(asin(clamp(toSun.y, -1.0f, 1.0f)));
    float _190;
    if (parameters.EnableDayNightCycle)
    {
        _190 = smoothstep(-6.0f, 6.0f, sunElevation);
    }
    else
    {
        _190 = 1.0f;
    }
    float dayAmount = _190;
    float _202;
    if (parameters.EnableDayNightCycle)
    {
        _202 = 1.0f - smoothstep(-18.0f, -6.0f, sunElevation);
    }
    else
    {
        _202 = 0.0f;
    }
    float nightAmount = _202;
    float _214;
    if (parameters.EnableDayNightCycle)
    {
        _214 = smoothstep(-18.0f, -6.0f, sunElevation) * (1.0f - smoothstep(2.0f, 12.0f, sunElevation));
    }
    else
    {
        _214 = 0.0f;
    }
    float twilightAmount = _214;
    float3 daylightBase = (h >= 0.0f) ? daySky : dayGround;
    float3 nightBase = (h >= 0.0f) ? nightSky : nightGround;
    float twilightWeight = max(0.0f, (1.0f - dayAmount) - nightAmount);
    float3 twilightBase = lerp(nightBase, daylightBase, smoothstep(-18.0f, 0.0f, sunElevation).xxx);
    float3 color = (((daylightBase * dayAmount) + (twilightBase * twilightWeight)) + (nightBase * nightAmount)) * parameters.SkyIntensity;
    float3 horizonDirection = normalize(float3(direction.x, 0.0f, direction.z) + float3(0.0f, 0.0f, 9.9999997473787516355514526367188e-06f));
    float3 horizonSun = normalize(float3(toSun.x, 0.0f, toSun.z) + float3(0.0f, 0.0f, 9.9999997473787516355514526367188e-06f));
    float towardSun = pow(max(dot(horizonDirection, horizonSun), 0.0f), 3.0f);
    float horizonBand = exp((-abs(h)) * 7.0f);
    float3 duskColor = lerp(float3(0.180000007152557373046875f, 0.02500000037252902984619140625f, 0.2199999988079071044921875f), float3(1.5499999523162841796875f, 0.20000000298023223876953125f, 0.02500000037252902984619140625f), towardSun.xxx);
    color += ((((duskColor * horizonBand) * twilightAmount) * (0.319999992847442626953125f + (0.680000007152557373046875f * towardSun))) * parameters.SkyIntensity);
    color += (((parameters.SunColor * pow(max(dot(direction, toSun), 0.0f), 12.0f)) * twilightAmount) * 0.100000001490116119384765625f);
    return color;
}

float3 EngineEvaluateBakedSun(float3 direction, float3 toSun, float3 sunColor, float sunIntensity, float angularRadius)
{
    float cosine = dot(direction, toSun);
    float disk = smoothstep(cos(angularRadius), cos(angularRadius * 0.699999988079071044921875f), cosine);
    float sunVisibility = smoothstep(-0.119999997317790985107421875f, 0.02999999932944774627685546875f, toSun.y);
    float aboveGround = smoothstep(-0.02999999932944774627685546875f, 0.0f, direction.y);
    float3 glow = sunColor * ((pow(clamp(cosine, 0.0f, 1.0f), 180.0f) * 0.5f) + (pow(clamp(cosine, 0.0f, 1.0f), 8.0f) * 0.0599999986588954925537109375f));
    return ((((sunColor * sunIntensity) * disk) * aboveGround) + glow) * sunVisibility;
}

float3 ProceduralRadiance(float3 direction)
{
    float3 toSun = normalize(-pc_SunDirectionIntensity.xyz);
    EngineProceduralSkyParameters parameters;
    parameters.ZenithColor = pc_ZenithIntensity.xyz;
    parameters.HorizonColor = pc_HorizonCycle.xyz;
    parameters.GroundColor = pc_GroundExposure.xyz;
    parameters.NightZenithColor = pc_NightZenithIntensity.xyz;
    parameters.NightHorizonColor = pc_NightHorizonGlow.xyz;
    parameters.SunColor = pc_SunColorAngularRadius.xyz;
    parameters.SkyIntensity = pc_ZenithIntensity.w;
    parameters.NightSkyIntensity = pc_NightZenithIntensity.w;
    parameters.NightHorizonGlow = pc_NightHorizonGlow.w;
    parameters.EnableDayNightCycle = pc_HorizonCycle.w > 0.5f;
    float3 param = direction;
    float3 param_1 = toSun;
    EngineProceduralSkyParameters param_2 = parameters;
    float3 color = EngineEvaluateProceduralSky(param, param_1, param_2);
    float3 param_3 = direction;
    float3 param_4 = toSun;
    float3 param_5 = pc_SunColorAngularRadius.xyz;
    float param_6 = pc_SunDirectionIntensity.w;
    float param_7 = pc_SunColorAngularRadius.w;
    color += EngineEvaluateBakedSun(param_3, param_4, param_5, param_6, param_7);
    return color;
}

void comp_main()
{
    int3 coordinate = int3(gl_GlobalInvocationID);
    uint _489_dummy_parameter;
    int3 size = int3(spvImageSize(outputCube, _489_dummy_parameter));
    bool _495 = coordinate.x >= size.x;
    bool _504;
    if (!_495)
    {
        _504 = coordinate.y >= size.y;
    }
    else
    {
        _504 = _495;
    }
    bool _511;
    if (!_504)
    {
        _511 = coordinate.z >= 6;
    }
    else
    {
        _511 = _504;
    }
    if (_511)
    {
        return;
    }
    float2 uv = (((float2(coordinate.xy) + 0.5f.xx) / float2(size.xy)) * 2.0f) - 1.0f.xx;
    uint param = uint(coordinate.z);
    float2 param_1 = uv;
    float3 direction = CubeDirection(param, param_1);
    float3 color;
    if (pc_BakeParameters.w > 0.5f)
    {
        float longitude = atan2(direction.z, direction.x) + pc_BakeParameters.z;
        float2 panoramaUv = float2(frac((longitude / 6.283185482025146484375f) + 0.5f), acos(clamp(direction.y, -1.0f, 1.0f)) / 3.1415927410125732421875f);
        color = max(panorama.SampleLevel(_panorama_sampler, panoramaUv, 0.0f).xyz, 0.0f.xxx) * pc_GroundExposure.w;
    }
    else
    {
        float3 param_2 = direction;
        color = ProceduralRadiance(param_2);
    }
    outputCube[coordinate] = float4(color, 1.0f);
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
