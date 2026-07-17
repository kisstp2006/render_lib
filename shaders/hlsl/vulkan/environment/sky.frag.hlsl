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

cbuffer FrameUniforms : register(b0, space0)
{
    row_major float4x4 frame_View : packoffset(c0);
    row_major float4x4 frame_Projection : packoffset(c4);
    row_major float4x4 frame_PreviousViewProjection : packoffset(c8);
    float4 frame_CameraPosition : packoffset(c12);
    float4 frame_SunDirectionIntensity : packoffset(c13);
    float4 frame_SunColor : packoffset(c14);
    float4 frame_SkyZenithIntensity : packoffset(c15);
    float4 frame_SkyHorizonPostEnabled : packoffset(c16);
    float4 frame_SkyGroundExposure : packoffset(c17);
    float4 frame_SkySunParameters : packoffset(c18);
    float4 frame_PostCurve0 : packoffset(c19);
    float4 frame_PostCurve1 : packoffset(c20);
    float4 frame_PostGrade : packoffset(c21);
    float4 frame_FogColorOpacity : packoffset(c22);
    float4 frame_FogStartEndExponents : packoffset(c23);
    float4 frame_FogHeightEnabled : packoffset(c24);
    float4 frame_NightZenithIntensity : packoffset(c25);
    float4 frame_NightHorizonGlow : packoffset(c26);
    float4 frame_MilkyWayColorIntensity : packoffset(c27);
    float4 frame_StarWarmDensity : packoffset(c28);
    float4 frame_StarCoolSize : packoffset(c29);
    float4 frame_StarAnimation : packoffset(c30);
    float4 frame_NightRotationFlags : packoffset(c31);
    float4 frame_SkyFeatureFlags : packoffset(c32);
    row_major float4x4 frame_CascadeMatrices[4] : packoffset(c33);
    float4 frame_CascadeSplits : packoffset(c49);
    float4 frame_ShadowParameters : packoffset(c50);
    uint4 frame_LightCounts : packoffset(c51);
    float4 frame_PointPositionRadius[8] : packoffset(c52);
    float4 frame_PointColor[8] : packoffset(c60);
    float4 frame_SpotPositionRange[4] : packoffset(c68);
    float4 frame_SpotDirectionCosOuter[4] : packoffset(c72);
    float4 frame_SpotColorCosInner[4] : packoffset(c76);
    float4 frame_AreaPositionRange[4] : packoffset(c80);
    float4 frame_AreaDirectionMinRoughness[4] : packoffset(c84);
    float4 frame_AreaRightHalfWidth[4] : packoffset(c88);
    float4 frame_AreaUpHalfHeight[4] : packoffset(c92);
    float4 frame_AreaColor[4] : packoffset(c96);
    float4 frame_AreaSoftness[4] : packoffset(c100);
    float4 frame_PointShadowCookie[8] : packoffset(c104);
    row_major float4x4 frame_SpotMatrices[4] : packoffset(c112);
    float4 frame_SpotShadowRects[4] : packoffset(c128);
    float4 frame_SpotCookieData[4] : packoffset(c132);
    row_major float4x4 frame_AreaMatrices[4] : packoffset(c136);
    float4 frame_AreaShadowRects[4] : packoffset(c152);
    float4 frame_AreaCookieData[4] : packoffset(c156);
};

TextureCube<float4> environmentMap : register(t8, space0);
SamplerState _environmentMap_sampler : register(s8, space0);

static float2 ndc;
static float2 outVelocity;
static float4 outColor;

struct SPIRV_Cross_Input
{
    float2 ndc : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 outColor : SV_Target0;
    float2 outVelocity : SV_Target1;
};

// Returns the determinant of a 2x2 matrix.
float spvDet2x2(float a1, float a2, float b1, float b2)
{
    return a1 * b2 - b1 * a2;
}

// Returns the determinant of a 3x3 matrix.
float spvDet3x3(float a1, float a2, float a3, float b1, float b2, float b3, float c1, float c2, float c3)
{
    return a1 * spvDet2x2(b2, b3, c2, c3) - b1 * spvDet2x2(a2, a3, c2, c3) + c1 * spvDet2x2(a2, a3, b2, b3);
}

// Returns the inverse of a matrix, by using the algorithm of calculating the classical
// adjoint and dividing by the determinant. The contents of the matrix are changed.
float4x4 spvInverse(float4x4 m)
{
    float4x4 adj;	// The adjoint matrix (inverse after dividing by determinant)

    // Create the transpose of the cofactors, as the classical adjoint of the matrix.
    adj[0][0] =  spvDet3x3(m[1][1], m[1][2], m[1][3], m[2][1], m[2][2], m[2][3], m[3][1], m[3][2], m[3][3]);
    adj[0][1] = -spvDet3x3(m[0][1], m[0][2], m[0][3], m[2][1], m[2][2], m[2][3], m[3][1], m[3][2], m[3][3]);
    adj[0][2] =  spvDet3x3(m[0][1], m[0][2], m[0][3], m[1][1], m[1][2], m[1][3], m[3][1], m[3][2], m[3][3]);
    adj[0][3] = -spvDet3x3(m[0][1], m[0][2], m[0][3], m[1][1], m[1][2], m[1][3], m[2][1], m[2][2], m[2][3]);

    adj[1][0] = -spvDet3x3(m[1][0], m[1][2], m[1][3], m[2][0], m[2][2], m[2][3], m[3][0], m[3][2], m[3][3]);
    adj[1][1] =  spvDet3x3(m[0][0], m[0][2], m[0][3], m[2][0], m[2][2], m[2][3], m[3][0], m[3][2], m[3][3]);
    adj[1][2] = -spvDet3x3(m[0][0], m[0][2], m[0][3], m[1][0], m[1][2], m[1][3], m[3][0], m[3][2], m[3][3]);
    adj[1][3] =  spvDet3x3(m[0][0], m[0][2], m[0][3], m[1][0], m[1][2], m[1][3], m[2][0], m[2][2], m[2][3]);

    adj[2][0] =  spvDet3x3(m[1][0], m[1][1], m[1][3], m[2][0], m[2][1], m[2][3], m[3][0], m[3][1], m[3][3]);
    adj[2][1] = -spvDet3x3(m[0][0], m[0][1], m[0][3], m[2][0], m[2][1], m[2][3], m[3][0], m[3][1], m[3][3]);
    adj[2][2] =  spvDet3x3(m[0][0], m[0][1], m[0][3], m[1][0], m[1][1], m[1][3], m[3][0], m[3][1], m[3][3]);
    adj[2][3] = -spvDet3x3(m[0][0], m[0][1], m[0][3], m[1][0], m[1][1], m[1][3], m[2][0], m[2][1], m[2][3]);

    adj[3][0] = -spvDet3x3(m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2], m[3][0], m[3][1], m[3][2]);
    adj[3][1] =  spvDet3x3(m[0][0], m[0][1], m[0][2], m[2][0], m[2][1], m[2][2], m[3][0], m[3][1], m[3][2]);
    adj[3][2] = -spvDet3x3(m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[3][0], m[3][1], m[3][2]);
    adj[3][3] =  spvDet3x3(m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]);

    // Calculate the determinant as a combination of the cofactors of the first row.
    float det = (adj[0][0] * m[0][0]) + (adj[0][1] * m[1][0]) + (adj[0][2] * m[2][0]) + (adj[0][3] * m[3][0]);

    // Divide the classical adjoint matrix by the determinant.
    // If determinant is zero, matrix is not invertable, so leave it unchanged.
    return (det != 0.0f) ? (adj * (1.0f / det)) : m;
}

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

float3 EvaluateProceduralSky(float3 direction, float3 toSun)
{
    EngineProceduralSkyParameters parameters;
    parameters.ZenithColor = frame_SkyZenithIntensity.xyz;
    parameters.HorizonColor = frame_SkyHorizonPostEnabled.xyz;
    parameters.GroundColor = frame_SkyGroundExposure.xyz;
    parameters.NightZenithColor = frame_NightZenithIntensity.xyz;
    parameters.NightHorizonColor = frame_NightHorizonGlow.xyz;
    parameters.SunColor = frame_SunColor.xyz;
    parameters.SkyIntensity = frame_SkyZenithIntensity.w;
    parameters.NightSkyIntensity = frame_NightZenithIntensity.w;
    parameters.NightHorizonGlow = frame_NightHorizonGlow.w;
    parameters.EnableDayNightCycle = frame_SkyFeatureFlags.x > 0.5f;
    float3 param = direction;
    float3 param_1 = toSun;
    EngineProceduralSkyParameters param_2 = parameters;
    return EngineEvaluateProceduralSky(param, param_1, param_2);
}

float2 EngineStarCubeGrid(float3 direction, out float faceIndex)
{
    float3 axis = abs(direction);
    bool _313 = axis.x >= axis.y;
    bool _321;
    if (_313)
    {
        _321 = axis.x >= axis.z;
    }
    else
    {
        _321 = _313;
    }
    float2 faceUv;
    if (_321)
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
    float2 _385 = EngineStarCubeGrid(param, param_1);
    float faceIndex = param_1;
    float2 starGrid = _385;
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
    float4 viewRay = mul(float4(ndc, 1.0f, 1.0f), spvInverse(frame_Projection));
    float3 viewDirection = normalize(viewRay.xyz / max(abs(viewRay.w), 9.9999997473787516355514526367188e-05f).xxx);
    float4x4 _683 = spvInverse(frame_View);
    float3 direction = normalize(mul(viewDirection, float3x3(_683[0].xyz, _683[1].xyz, _683[2].xyz)));
    float3 toSun = normalize(-frame_SunDirectionIntensity.xyz);
    float3 _705;
    if (frame_SkySunParameters.w > 0.5f)
    {
        float3 param = direction;
        float3 param_1 = toSun;
        _705 = EvaluateProceduralSky(param, param_1);
    }
    else
    {
        _705 = environmentMap.SampleLevel(_environmentMap_sampler, direction, 0.0f).xyz;
    }
    float3 color = _705;
    bool _725 = frame_SkySunParameters.w > 0.5f;
    bool _731;
    if (_725)
    {
        _731 = frame_SkySunParameters.x > 0.0f;
    }
    else
    {
        _731 = _725;
    }
    if (_731)
    {
        float angle = acos(clamp(dot(direction, toSun), -1.0f, 1.0f));
        float edgeWidth = max(fwidth(angle) * 1.5f, 4.9999998736893758177757263183594e-05f);
        float disk = 1.0f - smoothstep(frame_SkySunParameters.y - edgeWidth, frame_SkySunParameters.y + edgeWidth, angle);
        float halo = exp((-angle) / max(frame_SkySunParameters.y * 7.0f, 9.9999997473787516355514526367188e-05f));
        float aboveHorizon = smoothstep(-0.0199999995529651641845703125f, 0.02999999932944774627685546875f, toSun.y) * smoothstep(-0.0199999995529651641845703125f, 0.00999999977648258209228515625f, direction.y);
        color += ((frame_SunColor.xyz * ((disk * frame_SkySunParameters.x) + ((halo * sqrt(frame_SkySunParameters.x)) * 0.180000007152557373046875f))) * aboveHorizon);
    }
    if (frame_SkyFeatureFlags.x > 0.5f)
    {
        float sunElevation = degrees(asin(clamp(toSun.y, -1.0f, 1.0f)));
        float nightAmount = 1.0f - smoothstep(-18.0f, -6.0f, sunElevation);
        float _817;
        if (frame_SkyFeatureFlags.w > 0.5f)
        {
            _817 = frame_StarAnimation.w;
        }
        else
        {
            _817 = 0.0f;
        }
        float animationTime = _817;
        float skyRotation = radians(frame_NightRotationFlags.x + (animationTime * frame_NightRotationFlags.y));
        float2x2 rotation = float2x2(float2(cos(skyRotation), -sin(skyRotation)), float2(sin(skyRotation), cos(skyRotation)));
        float2 rotatedXZ = mul(direction.xz, rotation);
        float3 nightDirection = normalize(float3(rotatedXZ.x, direction.y, rotatedXZ.y));
        float3 param_2 = nightDirection;
        float param_3 = frame_StarWarmDensity.w;
        float param_4 = frame_StarCoolSize.w;
        EngineProceduralStar star = EngineEvaluateProceduralStar(param_2, param_3, param_4);
        float3 starColor = lerp(frame_StarWarmDensity.xyz, frame_StarCoolSize.xyz, star.Temperature.xxx);
        float horizonFade = smoothstep(-0.02999999932944774627685546875f, 0.14000000059604644775390625f, direction.y);
        float twinkle = lerp(1.0f, 0.7200000286102294921875f + (0.2800000011920928955078125f * sin(((animationTime * star.TwinkleSpeed) * max(frame_StarAnimation.z, 0.0f)) + star.TwinklePhase)), clamp(frame_StarAnimation.y, 0.0f, 1.0f));
        if (frame_SkyFeatureFlags.y > 0.5f)
        {
            color += (((((starColor * star.Shape) * frame_StarAnimation.x) * twinkle) * nightAmount) * horizonFade);
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
        if (frame_SkyFeatureFlags.z > 0.5f)
        {
            color += (((((frame_MilkyWayColorIntensity.xyz * band) * (0.20000000298023223876953125f + (dust * 0.800000011920928955078125f))) * frame_MilkyWayColorIntensity.w) * nightAmount) * smoothstep(0.0f, 0.180000007152557373046875f, direction.y));
        }
    }
    color *= frame_SkySunParameters.z;
    float4 previousClip = mul(float4(direction, 0.0f), frame_PreviousViewProjection);
    float2 previousUv = ((previousClip.xy / max(previousClip.w, 9.9999999747524270787835121154785e-07f).xx) * 0.5f) + 0.5f.xx;
    outVelocity = ((ndc * 0.5f) + 0.5f.xx) - previousUv;
    outColor = float4(color, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    ndc = stage_input.ndc;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.outVelocity = outVelocity;
    stage_output.outColor = outColor;
    return stage_output;
}
