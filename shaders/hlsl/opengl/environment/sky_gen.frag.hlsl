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

cbuffer EngineGlobals_environment_sky_gen_frag_hlsl : register(b13)
{
    row_major float3x3 uFaceBasis;
    float3 uSunDirection;
    float3 uZenithColor;
    float3 uHorizonColor;
    float3 uGroundColor;
    float3 uNightZenithColor;
    float3 uNightHorizonColor;
    float3 uSunColor;
    float uSkyIntensity;
    float uNightSkyIntensity;
    float uNightHorizonGlow;
    bool uEnableDayNightCycle;
    float uSunIntensity;
    float uSunAngularRadius;
};


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

float3 EngineEvaluateProceduralSky(float3 direction, float3 toSun, EngineProceduralSkyParameters parameters)
{
    float h = direction.y;
    float3 daySky = lerp(parameters.HorizonColor, parameters.ZenithColor, pow(clamp(h, 0.0f, 1.0f), 0.550000011920928955078125f).xxx);
    float3 dayGround = lerp(parameters.HorizonColor * 0.85000002384185791015625f, parameters.GroundColor, pow(clamp(-h, 0.0f, 1.0f), 0.4000000059604644775390625f).xxx);
    float3 nightSky = lerp(parameters.NightHorizonColor * parameters.NightHorizonGlow, parameters.NightZenithColor, pow(clamp(h, 0.0f, 1.0f), 0.449999988079071044921875f).xxx) * parameters.NightSkyIntensity;
    float3 nightGround = parameters.NightZenithColor * (0.119999997317790985107421875f * parameters.NightSkyIntensity);
    float sunElevation = degrees(asin(clamp(toSun.y, -1.0f, 1.0f)));
    float _104;
    if (parameters.EnableDayNightCycle)
    {
        _104 = smoothstep(-6.0f, 6.0f, sunElevation);
    }
    else
    {
        _104 = 1.0f;
    }
    float dayAmount = _104;
    float _116;
    if (parameters.EnableDayNightCycle)
    {
        _116 = 1.0f - smoothstep(-18.0f, -6.0f, sunElevation);
    }
    else
    {
        _116 = 0.0f;
    }
    float nightAmount = _116;
    float _128;
    if (parameters.EnableDayNightCycle)
    {
        _128 = smoothstep(-18.0f, -6.0f, sunElevation) * (1.0f - smoothstep(2.0f, 12.0f, sunElevation));
    }
    else
    {
        _128 = 0.0f;
    }
    float twilightAmount = _128;
    bool3 _147 = (h >= 0.0f).xxx;
    float3 daylightBase = float3(_147.x ? daySky.x : dayGround.x, _147.y ? daySky.y : dayGround.y, _147.z ? daySky.z : dayGround.z);
    bool3 _154 = (h >= 0.0f).xxx;
    float3 nightBase = float3(_154.x ? nightSky.x : nightGround.x, _154.y ? nightSky.y : nightGround.y, _154.z ? nightSky.z : nightGround.z);
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

void frag_main()
{
    float3 dir = normalize(mul(float3(vNdc, 1.0f), uFaceBasis));
    float3 toSun = normalize(-uSunDirection);
    EngineProceduralSkyParameters parameters;
    parameters.ZenithColor = uZenithColor;
    parameters.HorizonColor = uHorizonColor;
    parameters.GroundColor = uGroundColor;
    parameters.NightZenithColor = uNightZenithColor;
    parameters.NightHorizonColor = uNightHorizonColor;
    parameters.SunColor = uSunColor;
    parameters.SkyIntensity = uSkyIntensity;
    parameters.NightSkyIntensity = uNightSkyIntensity;
    parameters.NightHorizonGlow = uNightHorizonGlow;
    parameters.EnableDayNightCycle = uEnableDayNightCycle;
    float3 param = dir;
    float3 param_1 = toSun;
    EngineProceduralSkyParameters param_2 = parameters;
    float3 color = EngineEvaluateProceduralSky(param, param_1, param_2);
    float3 param_3 = dir;
    float3 param_4 = toSun;
    float3 param_5 = uSunColor;
    float param_6 = uSunIntensity;
    float param_7 = uSunAngularRadius;
    color += EngineEvaluateBakedSun(param_3, param_4, param_5, param_6, param_7);
    FragColor = float4(color, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vNdc = stage_input.vNdc;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
