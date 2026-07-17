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

struct EngineProceduralStar
{
    float Shape;
    float Temperature;
    float TwinklePhase;
    float TwinkleSpeed;
};

cbuffer EngineGlobals_environment_sky_frag_hlsl : register(b13)
{
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
    row_major float4x4 uInvProj;
    row_major float4x4 uInvView;
    float3 uSunDirection;
    bool uUseProceduralSky;
    bool uDrawProceduralSun;
    float uSunAngularRadius;
    float uSunIntensity;
    bool uAnimateNightSky;
    float uTime;
    float uNightSkyRotationDegrees;
    float uNightSkyRotationSpeed;
    float uStarDensity;
    float uStarSize;
    float3 uStarWarmColor;
    float3 uStarCoolColor;
    float uStarTwinkleSpeed;
    float uStarTwinkle;
    bool uStarsEnabled;
    float uStarIntensity;
    bool uMilkyWayEnabled;
    float3 uMilkyWayColor;
    float uMilkyWayIntensity;
    float uBackgroundMultiplier;
    row_major float4x4 uPreviousViewProjection;
};

TextureCube<float4> uEnvMap : register(t0);
SamplerState _uEnvMap_sampler : register(s0);

static float2 vNdc;
static float4 FragColor;
static float2 OutVelocity;

struct SPIRV_Cross_Input
{
    float2 vNdc : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
    float2 OutVelocity : SV_Target1;
};

float3 EngineEvaluateProceduralSky(float3 direction, float3 toSun, EngineProceduralSkyParameters parameters)
{
    float h = direction.y;
    float3 daySky = lerp(parameters.HorizonColor, parameters.ZenithColor, pow(clamp(h, 0.0f, 1.0f), 0.550000011920928955078125f).xxx);
    float3 dayGround = lerp(parameters.HorizonColor * 0.85000002384185791015625f, parameters.GroundColor, pow(clamp(-h, 0.0f, 1.0f), 0.4000000059604644775390625f).xxx);
    float3 nightSky = lerp(parameters.NightHorizonColor * parameters.NightHorizonGlow, parameters.NightZenithColor, pow(clamp(h, 0.0f, 1.0f), 0.449999988079071044921875f).xxx) * parameters.NightSkyIntensity;
    float3 nightGround = parameters.NightZenithColor * (0.119999997317790985107421875f * parameters.NightSkyIntensity);
    float sunElevation = degrees(asin(clamp(toSun.y, -1.0f, 1.0f)));
    float _122;
    if (parameters.EnableDayNightCycle)
    {
        _122 = smoothstep(-6.0f, 6.0f, sunElevation);
    }
    else
    {
        _122 = 1.0f;
    }
    float dayAmount = _122;
    float _134;
    if (parameters.EnableDayNightCycle)
    {
        _134 = 1.0f - smoothstep(-18.0f, -6.0f, sunElevation);
    }
    else
    {
        _134 = 0.0f;
    }
    float nightAmount = _134;
    float _146;
    if (parameters.EnableDayNightCycle)
    {
        _146 = smoothstep(-18.0f, -6.0f, sunElevation) * (1.0f - smoothstep(2.0f, 12.0f, sunElevation));
    }
    else
    {
        _146 = 0.0f;
    }
    float twilightAmount = _146;
    bool3 _165 = (h >= 0.0f).xxx;
    float3 daylightBase = float3(_165.x ? daySky.x : dayGround.x, _165.y ? daySky.y : dayGround.y, _165.z ? daySky.z : dayGround.z);
    bool3 _172 = (h >= 0.0f).xxx;
    float3 nightBase = float3(_172.x ? nightSky.x : nightGround.x, _172.y ? nightSky.y : nightGround.y, _172.z ? nightSky.z : nightGround.z);
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

float3 EvaluateProceduralSky(float3 direction, float3 toSun)
{
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
    float3 param = direction;
    float3 param_1 = toSun;
    EngineProceduralSkyParameters param_2 = parameters;
    return EngineEvaluateProceduralSky(param, param_1, param_2);
}

float2 EngineStarCubeGrid(float3 direction, out float faceIndex)
{
    float3 axis = abs(direction);
    bool _316 = axis.x >= axis.y;
    bool _324;
    if (_316)
    {
        _324 = axis.x >= axis.z;
    }
    else
    {
        _324 = _316;
    }
    float2 faceUv;
    if (_324)
    {
        faceUv = direction.zy / max(axis.x, 9.9999999747524270787835121154785e-07f).xx;
        faceIndex = (direction.x >= 0.0f) ? 0.0f : 1.0f;
    }
    else
    {
        if (axis.y >= axis.z)
        {
            faceUv = direction.xz / max(axis.y, 9.9999999747524270787835121154785e-07f).xx;
            faceIndex = (direction.y >= 0.0f) ? 2.0f : 3.0f;
        }
        else
        {
            faceUv = direction.xy / max(axis.z, 9.9999999747524270787835121154785e-07f).xx;
            faceIndex = (direction.z >= 0.0f) ? 4.0f : 5.0f;
        }
    }
    return ((faceUv * 0.5f) + 0.5f.xx) * 208.0f;
}

float EngineStarHash(float2 p)
{
    float3 p3 = frac(p.xyx * 0.103100001811981201171875f);
    p3 += dot(p3, p3.yzx + 33.3300018310546875f.xxx).xxx;
    return frac((p3.x + p3.y) * p3.z);
}

EngineProceduralStar EngineEvaluateProceduralStar(float3 direction, float density, float size)
{
    float3 param = normalize(direction);
    float param_1;
    float2 _388 = EngineStarCubeGrid(param, param_1);
    float faceIndex = param_1;
    float2 starGrid = _388;
    float2 starCell = floor(starGrid);
    float2 starLocal = frac(starGrid) - 0.5f.xx;
    float2 hashCell = starCell + float2(faceIndex * 277.0f, faceIndex * 619.0f);
    float2 param_2 = hashCell;
    float seed = EngineStarHash(param_2);
    float2 param_3 = hashCell + 19.700000762939453125f.xx;
    float radius = lerp(0.04500000178813934326171875f, 0.1599999964237213134765625f, EngineStarHash(param_3)) * clamp(size, 0.25f, 4.0f);
    float antialias = max(length(fwidth(starGrid)) * 0.2199999988079071044921875f, 0.01200000010430812835693359375f);
    float core = 1.0f - smoothstep(radius, radius + antialias, length(starLocal));
    float2 param_4 = hashCell + 91.3000030517578125f.xx;
    float rareBrightness = pow(EngineStarHash(param_4), 8.0f);
    float horizontalRay = exp((-abs(starLocal.x)) * 34.0f) * exp((-abs(starLocal.y)) * 5.0f);
    float verticalRay = exp((-abs(starLocal.y)) * 34.0f) * exp((-abs(starLocal.x)) * 5.0f);
    EngineProceduralStar result;
    result.Shape = max(core, ((horizontalRay + verticalRay) * rareBrightness) * 0.319999992847442626953125f) * step(1.0f - clamp(density, 0.0f, 0.0500000007450580596923828125f), seed);
    float2 param_5 = hashCell + 47.200000762939453125f.xx;
    result.Temperature = EngineStarHash(param_5);
    float2 param_6 = hashCell + 73.09999847412109375f.xx;
    result.TwinklePhase = (EngineStarHash(param_6) * 2.0f) * 3.1415927410125732421875f;
    float2 param_7 = hashCell + 12.3999996185302734375f.xx;
    result.TwinkleSpeed = lerp(0.699999988079071044921875f, 2.400000095367431640625f, EngineStarHash(param_7));
    return result;
}

float ValueNoise(float2 p)
{
    float2 cell = floor(p);
    float2 fraction = frac(p);
    fraction = (fraction * fraction) * (3.0f.xx - (fraction * 2.0f));
    float2 param = cell;
    float a = EngineStarHash(param);
    float2 param_1 = cell + float2(1.0f, 0.0f);
    float b = EngineStarHash(param_1);
    float2 param_2 = cell + float2(0.0f, 1.0f);
    float c = EngineStarHash(param_2);
    float2 param_3 = cell + 1.0f.xx;
    float d = EngineStarHash(param_3);
    return lerp(lerp(a, b, fraction.x), lerp(c, d, fraction.x), fraction.y);
}

void frag_main()
{
    float4 viewRay = mul(float4(vNdc, 1.0f, 1.0f), uInvProj);
    float3 dirView = normalize(viewRay.xyz / viewRay.w.xxx);
    float3 dirWorld = normalize(mul(dirView, float3x3(uInvView[0].xyz, uInvView[1].xyz, uInvView[2].xyz)));
    float3 toSun = normalize(-uSunDirection);
    float3 _667;
    if (uUseProceduralSky)
    {
        float3 param = dirWorld;
        float3 param_1 = toSun;
        _667 = EvaluateProceduralSky(param, param_1);
    }
    else
    {
        _667 = uEnvMap.SampleLevel(_uEnvMap_sampler, dirWorld, 0.0f).xyz;
    }
    float3 color = _667;
    if (uDrawProceduralSun)
    {
        float angle = acos(clamp(dot(dirWorld, toSun), -1.0f, 1.0f));
        float edgeWidth = max(fwidth(angle) * 1.5f, 4.9999998736893758177757263183594e-05f);
        float disk = 1.0f - smoothstep(uSunAngularRadius - edgeWidth, uSunAngularRadius + edgeWidth, angle);
        float halo = exp((-angle) / max(uSunAngularRadius * 7.0f, 9.9999997473787516355514526367188e-05f));
        float aboveHorizon = smoothstep(-0.0199999995529651641845703125f, 0.02999999932944774627685546875f, toSun.y) * smoothstep(-0.0199999995529651641845703125f, 0.00999999977648258209228515625f, dirWorld.y);
        color += ((uSunColor * ((disk * uSunIntensity) + ((halo * sqrt(max(uSunIntensity, 0.0f))) * 0.180000007152557373046875f))) * aboveHorizon);
    }
    if (uEnableDayNightCycle)
    {
        float sunElevation = degrees(asin(clamp(toSun.y, -1.0f, 1.0f)));
        float nightAmount = 1.0f - smoothstep(-18.0f, -6.0f, sunElevation);
        float animationTime = uAnimateNightSky ? uTime : 0.0f;
        float skyRotation = radians(uNightSkyRotationDegrees + (animationTime * uNightSkyRotationSpeed));
        float2x2 skyRotationMatrix = float2x2(float2(cos(skyRotation), -sin(skyRotation)), float2(sin(skyRotation), cos(skyRotation)));
        float2 rotatedXZ = mul(dirWorld.xz, skyRotationMatrix);
        float3 nightDirection = normalize(float3(rotatedXZ.x, dirWorld.y, rotatedXZ.y));
        float3 param_2 = nightDirection;
        float param_3 = uStarDensity;
        float param_4 = uStarSize;
        EngineProceduralStar star = EngineEvaluateProceduralStar(param_2, param_3, param_4);
        float3 starColor = lerp(uStarWarmColor, uStarCoolColor, star.Temperature.xxx);
        float horizonFade = smoothstep(-0.02999999932944774627685546875f, 0.14000000059604644775390625f, dirWorld.y);
        float twinkle = lerp(1.0f, 0.7200000286102294921875f + (0.2800000011920928955078125f * sin(((animationTime * star.TwinkleSpeed) * max(uStarTwinkleSpeed, 0.0f)) + star.TwinklePhase)), clamp(uStarTwinkle, 0.0f, 1.0f));
        if (uStarsEnabled)
        {
            color += (((((starColor * star.Shape) * uStarIntensity) * twinkle) * nightAmount) * horizonFade);
        }
        float3 galaxyNormal = float3(0.240771710872650146484375f, 0.9630868434906005859375f, 0.1203858554363250732421875f);
        float3 galaxyRight = normalize(cross(float3(0.0f, 1.0f, 0.0f), galaxyNormal));
        float3 galaxyUp = cross(galaxyNormal, galaxyRight);
        float distanceToBand = abs(dot(nightDirection, galaxyNormal));
        float band = 1.0f - smoothstep(0.0500000007450580596923828125f, 0.3400000035762786865234375f, distanceToBand);
        float2 galaxyUv = float2(dot(nightDirection, galaxyRight), dot(nightDirection, galaxyUp));
        float2 param_5 = galaxyUv * 8.0f;
        float2 param_6 = galaxyUv * 23.0f;
        float dust = (ValueNoise(param_5) * 0.64999997615814208984375f) + (ValueNoise(param_6) * 0.3499999940395355224609375f);
        dust = smoothstep(0.2199999988079071044921875f, 0.87999999523162841796875f, dust);
        if (uMilkyWayEnabled)
        {
            color += (((((uMilkyWayColor * band) * (0.20000000298023223876953125f + (dust * 0.800000011920928955078125f))) * uMilkyWayIntensity) * nightAmount) * smoothstep(0.0f, 0.180000007152557373046875f, dirWorld.y));
        }
    }
    FragColor = float4(color * uBackgroundMultiplier, 1.0f);
    float4 previousClip = mul(float4(dirWorld, 0.0f), uPreviousViewProjection);
    float2 previousUv = ((previousClip.xy / max(previousClip.w, 9.9999999747524270787835121154785e-07f).xx) * 0.5f) + 0.5f.xx;
    OutVelocity = ((vNdc * 0.5f) + 0.5f.xx) - previousUv;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vNdc = stage_input.vNdc;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    stage_output.OutVelocity = OutVelocity;
    return stage_output;
}
