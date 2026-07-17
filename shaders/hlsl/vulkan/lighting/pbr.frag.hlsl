static const float3 _854[8] = { 1.0f.xxx, float3(-1.0f, 1.0f, 1.0f), float3(1.0f, -1.0f, 1.0f), float3(-1.0f, -1.0f, 1.0f), float3(1.0f, 1.0f, -1.0f), float3(-1.0f, 1.0f, -1.0f), float3(1.0f, -1.0f, -1.0f), (-1.0f).xxx };
static const float3 _1781[4] = { float3(1.0f, 0.25f, 0.25f), float3(0.25f, 1.0f, 0.25f), float3(0.25f, 0.449999988079071044921875f, 1.0f), float3(1.0f, 0.85000002384185791015625f, 0.20000000298023223876953125f) };

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

cbuffer MaterialUniforms : register(b0, space1)
{
    float4 material_BaseColorFactor : packoffset(c0);
    float4 material_EmissiveMetallic : packoffset(c1);
    float4 material_RoughnessAoAlphaCutoff : packoffset(c2);
    uint4 material_TextureFlags : packoffset(c3);
};

Texture2D<float4> shadowMap0 : register(t4, space0);
SamplerState _shadowMap0_sampler : register(s4, space0);
Texture2D<float4> shadowMap1 : register(t5, space0);
SamplerState _shadowMap1_sampler : register(s5, space0);
Texture2D<float4> shadowMap2 : register(t6, space0);
SamplerState _shadowMap2_sampler : register(s6, space0);
Texture2D<float4> shadowMap3 : register(t7, space0);
SamplerState _shadowMap3_sampler : register(s7, space0);
Texture2D<float4> lightCookieAtlas : register(t11, space0);
SamplerState _lightCookieAtlas_sampler : register(s11, space0);
Texture2D<float4> localShadowAtlas : register(t10, space0);
SamplerState _localShadowAtlas_sampler : register(s10, space0);
TextureCubeArray<float4> pointShadowMaps : register(t9, space0);
SamplerState _pointShadowMaps_sampler : register(s9, space0);
Texture2D<float4> baseColorMap : register(t1, space1);
SamplerState _baseColorMap_sampler : register(s1, space1);
Texture2D<float4> metallicRoughnessMap : register(t3, space1);
SamplerState _metallicRoughnessMap_sampler : register(s3, space1);
Texture2D<float4> normalMap : register(t2, space1);
SamplerState _normalMap_sampler : register(s2, space1);
Texture2D<float4> occlusionMap : register(t4, space1);
SamplerState _occlusionMap_sampler : register(s4, space1);
Texture2D<float4> emissiveMap : register(t5, space1);
SamplerState _emissiveMap_sampler : register(s5, space1);
TextureCube<float4> irradianceMap : register(t1, space0);
SamplerState _irradianceMap_sampler : register(s1, space0);
TextureCube<float4> prefilteredMap : register(t2, space0);
SamplerState _prefilteredMap_sampler : register(s2, space0);
Texture2D<float4> brdfLut : register(t3, space0);
SamplerState _brdfLut_sampler : register(s3, space0);

static float2 uv;
static float3 worldNormal;
static float4 worldTangent;
static float3 worldPosition;
static float4 currentClip;
static float4 previousClip;
static float2 outVelocity;
static float4 outColor;

struct SPIRV_Cross_Input
{
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float4 worldTangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float4 currentClip : TEXCOORD4;
    float4 previousClip : TEXCOORD5;
};

struct SPIRV_Cross_Output
{
    float4 outColor : SV_Target0;
    float2 outVelocity : SV_Target1;
};

uint2 spvTextureSize(Texture2D<float4> Tex, uint Level, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(Level, ret.x, ret.y, Param);
    return ret;
}

float2 ShadowTexelSize(int cascade)
{
    if (cascade == 0)
    {
        uint _315_dummy_parameter;
        return 1.0f.xx / float2(int2(spvTextureSize(shadowMap0, uint(0), _315_dummy_parameter)));
    }
    if (cascade == 1)
    {
        uint _326_dummy_parameter;
        return 1.0f.xx / float2(int2(spvTextureSize(shadowMap1, uint(0), _326_dummy_parameter)));
    }
    if (cascade == 2)
    {
        uint _337_dummy_parameter;
        return 1.0f.xx / float2(int2(spvTextureSize(shadowMap2, uint(0), _337_dummy_parameter)));
    }
    uint _344_dummy_parameter;
    return 1.0f.xx / float2(int2(spvTextureSize(shadowMap3, uint(0), _344_dummy_parameter)));
}

float ReadShadowDepth(int cascade, float2 uv_1)
{
    if (cascade == 0)
    {
        return shadowMap0.Sample(_shadowMap0_sampler, uv_1).x;
    }
    if (cascade == 1)
    {
        return shadowMap1.Sample(_shadowMap1_sampler, uv_1).x;
    }
    if (cascade == 2)
    {
        return shadowMap2.Sample(_shadowMap2_sampler, uv_1).x;
    }
    return shadowMap3.Sample(_shadowMap3_sampler, uv_1).x;
}

float SampleCascadeShadow(int cascade, float3 position, float nDotL)
{
    float4 lightPosition = mul(float4(position, 1.0f), frame_CascadeMatrices[cascade]);
    float3 projected = lightPosition.xyz / lightPosition.w.xxx;
    float2 uv_1 = (projected.xy * 0.5f) + 0.5f.xx;
    bool _405 = projected.z < 0.0f;
    bool _412;
    if (!_405)
    {
        _412 = projected.z > 1.0f;
    }
    else
    {
        _412 = _405;
    }
    bool _421;
    if (!_412)
    {
        _421 = any(bool2(uv_1.x < 0.0f.xx.x, uv_1.y < 0.0f.xx.y));
    }
    else
    {
        _421 = _412;
    }
    bool _429;
    if (!_421)
    {
        _429 = any(bool2(uv_1.x > 1.0f.xx.x, uv_1.y > 1.0f.xx.y));
    }
    else
    {
        _429 = _421;
    }
    if (_429)
    {
        return 1.0f;
    }
    float bias = max(0.0024999999441206455230712890625f * (1.0f - nDotL), 0.0005000000237487256526947021484375f);
    int param = cascade;
    float2 texel = ShadowTexelSize(param);
    float visibility = 0.0f;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            int param_1 = cascade;
            float2 param_2 = uv_1 + (float2(float(x), float(y)) * texel);
            visibility += float((projected.z - bias) <= ReadShadowDepth(param_1, param_2));
        }
    }
    return visibility / 9.0f;
}

float SampleSunShadow(float3 position, float nDotL, inout int activeCascade)
{
    float viewDepth = -mul(float4(position, 1.0f), frame_View).z;
    activeCascade = 3;
    for (int cascade = 0; cascade < 4; cascade++)
    {
        if (viewDepth <= frame_CascadeSplits[cascade])
        {
            activeCascade = cascade;
            break;
        }
    }
    if (viewDepth > frame_CascadeSplits.w)
    {
        return 1.0f;
    }
    int param = activeCascade;
    float3 param_1 = position;
    float param_2 = nDotL;
    float visibility = SampleCascadeShadow(param, param_1, param_2);
    if (activeCascade < 3)
    {
        float _548;
        if (activeCascade == 0)
        {
            _548 = 0.0f;
        }
        else
        {
            _548 = frame_CascadeSplits[activeCascade - 1];
        }
        float nearDepth = _548;
        float blendWidth = (frame_CascadeSplits[activeCascade] - nearDepth) * frame_ShadowParameters.x;
        float blendStart = frame_CascadeSplits[activeCascade] - blendWidth;
        if (viewDepth > blendStart)
        {
            float blend = clamp((viewDepth - blendStart) / max(blendWidth, 9.9999997473787516355514526367188e-05f), 0.0f, 1.0f);
            int param_3 = activeCascade + 1;
            float3 param_4 = position;
            float param_5 = nDotL;
            visibility = lerp(visibility, SampleCascadeShadow(param_3, param_4, param_5), blend);
        }
    }
    return visibility;
}

float D_GGX(float nDotH, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = (((nDotH * alphaSquared) - nDotH) * nDotH) + 1.0f;
    return alphaSquared / max((3.1415927410125732421875f * denominator) * denominator, 1.0000000116860974230803549289703e-07f);
}

float G_SchlickSmithGGX(float nDotL, float nDotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    float visibilityL = (nDotL * (1.0f - k)) + k;
    float visibilityV = (nDotV * (1.0f - k)) + k;
    return 1.0f / max((4.0f * visibilityL) * visibilityV, 1.0000000116860974230803549289703e-07f);
}

float3 F_Schlick(float cosine, float3 f0)
{
    return f0 + ((1.0f.xxx - f0) * pow(clamp(1.0f - cosine, 0.0f, 1.0f), 5.0f));
}

float3 EvaluateLight(float3 normal, float3 viewDirection, float3 lightDirection, float3 radiance, float3 albedo, float3 f0, float roughness, float metallic)
{
    float3 halfway = normalize(viewDirection + lightDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0f);
    float nDotV = max(dot(normal, viewDirection), 9.9999997473787516355514526367188e-05f);
    float nDotH = max(dot(normal, halfway), 0.0f);
    float vDotH = max(dot(viewDirection, halfway), 0.0f);
    float param = nDotH;
    float param_1 = roughness;
    float distribution = D_GGX(param, param_1);
    float param_2 = nDotL;
    float param_3 = nDotV;
    float param_4 = roughness;
    float visibility = G_SchlickSmithGGX(param_2, param_3, param_4);
    float param_5 = vDotH;
    float3 param_6 = f0;
    float3 fresnel = F_Schlick(param_5, param_6);
    float3 specular = fresnel * (distribution * visibility);
    float3 diffuse = (((1.0f.xxx - fresnel) * (1.0f - metallic)) * albedo) / 3.1415927410125732421875f.xxx;
    return ((diffuse + specular) * radiance) * nDotL;
}

float SamplePointShadow(uint lightIndex, float3 position)
{
    int shadowIndex = int(round(frame_PointShadowCookie[lightIndex].x));
    if (shadowIndex < 0)
    {
        return 1.0f;
    }
    float3 fromLight = position - frame_PointPositionRadius[lightIndex].xyz;
    float distanceToLight = length(fromLight);
    float3 direction = fromLight / max(distanceToLight, 9.9999997473787516355514526367188e-05f).xxx;
    float radius = frame_PointPositionRadius[lightIndex].w;
    float disk = 0.0030000000260770320892333984375f + ((0.0199999995529651641845703125f * distanceToLight) / radius);
    float visibility = 0.0f;
    for (int i = 0; i < 8; i++)
    {
        float stored = pointShadowMaps.Sample(_pointShadowMaps_sampler, float4(direction + (_854[i] * disk), float(shadowIndex))).x * radius;
        visibility += float((distanceToLight - 0.0350000001490116119384765625f) <= stored);
    }
    return visibility / 8.0f;
}

float SampleCookie(int slot, float2 cookieUv)
{
    if (slot <= 0)
    {
        return 1.0f;
    }
    float2 offset = float2(float(slot % 4), float(slot / 4)) * 0.25f;
    return lightCookieAtlas.Sample(_lightCookieAtlas_sampler, offset + (clamp(cookieUv, 0.0f.xx, 1.0f.xx) * 0.25f)).x;
}

float SamplePointCookie(uint lightIndex, float3 position)
{
    int slot = int(round(frame_PointShadowCookie[lightIndex].y));
    if (slot <= 0)
    {
        return 1.0f;
    }
    float3 direction = normalize(position - frame_PointPositionRadius[lightIndex].xyz);
    float2 cookieUv = float2((atan2(direction.z, direction.x) / 6.283185482025146484375f) + 0.5f, (asin(clamp(direction.y, -1.0f, 1.0f)) / 3.1415927410125732421875f) + 0.5f);
    int param = slot;
    float2 param_1 = cookieUv;
    return SampleCookie(param, param_1);
}

bool ProjectLocalLight(float4x4 _matrix, float3 position, inout float2 projectedUv, inout float depth)
{
    float4 clip = mul(float4(position, 1.0f), _matrix);
    if (clip.w <= 0.0f)
    {
        return false;
    }
    float3 projected = clip.xyz / clip.w.xxx;
    projectedUv = (projected.xy * 0.5f) + 0.5f.xx;
    depth = projected.z;
    bool _658 = depth >= 0.0f;
    bool _663;
    if (_658)
    {
        _663 = depth <= 1.0f;
    }
    else
    {
        _663 = _658;
    }
    bool _669;
    if (_663)
    {
        _669 = all(bool2(projectedUv.x >= 0.0f.xx.x, projectedUv.y >= 0.0f.xx.y));
    }
    else
    {
        _669 = _663;
    }
    bool _675;
    if (_669)
    {
        _675 = all(bool2(projectedUv.x <= 1.0f.xx.x, projectedUv.y <= 1.0f.xx.y));
    }
    else
    {
        _675 = _669;
    }
    return _675;
}

float SampleProjectedShadow(float4x4 _matrix, float4 atlasRect, float3 position, float nDotL)
{
    if (atlasRect.z <= 0.0f)
    {
        return 1.0f;
    }
    float4x4 param = _matrix;
    float3 param_1 = position;
    float2 param_2;
    float param_3;
    bool _692 = ProjectLocalLight(param, param_1, param_2, param_3);
    float2 projectedUv = param_2;
    float depth = param_3;
    if (!_692)
    {
        return 1.0f;
    }
    float bias = max(0.00200000009499490261077880859375f * (1.0f - nDotL), 0.00039999998989515006542205810546875f);
    uint _710_dummy_parameter;
    float2 texel = 1.0f.xx / float2(int2(spvTextureSize(localShadowAtlas, uint(0), _710_dummy_parameter)));
    float2 atlasUv = atlasRect.xy + (projectedUv * atlasRect.zw);
    float2 atlasMin = atlasRect.xy + (texel * 0.5f);
    float2 atlasMax = (atlasRect.xy + atlasRect.zw) - (texel * 0.5f);
    float visibility = 0.0f;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float2 sampleUv = clamp(atlasUv + (float2(float(x), float(y)) * texel), atlasMin, atlasMax);
            visibility += float((depth - bias) <= localShadowAtlas.Sample(_localShadowAtlas_sampler, sampleUv).x);
        }
    }
    return visibility / 9.0f;
}

float3 F_SchlickRoughness(float cosine, float3 f0, float roughness)
{
    return f0 + ((max((1.0f - roughness).xxx, f0) - f0) * pow(clamp(1.0f - cosine, 0.0f, 1.0f), 5.0f));
}

void frag_main()
{
    float4 baseColor = material_BaseColorFactor;
    uint flags = material_TextureFlags.x;
    if ((flags & 1u) != 0u)
    {
        baseColor *= baseColorMap.Sample(_baseColorMap_sampler, uv);
    }
    bool _957 = (flags & 32u) != 0u;
    bool _965;
    if (_957)
    {
        _965 = baseColor.w < material_RoughnessAoAlphaCutoff.z;
    }
    else
    {
        _965 = _957;
    }
    if (_965)
    {
        discard;
    }
    float metallic = material_EmissiveMetallic.w;
    float roughness = material_RoughnessAoAlphaCutoff.x;
    if ((flags & 4u) != 0u)
    {
        float4 mr = metallicRoughnessMap.Sample(_metallicRoughnessMap_sampler, uv);
        roughness *= mr.y;
        metallic *= mr.z;
    }
    else
    {
        if ((flags & 64u) != 0u)
        {
            float4 mrao = metallicRoughnessMap.Sample(_metallicRoughnessMap_sampler, uv);
            metallic *= mrao.x;
            roughness *= mrao.y;
        }
    }
    roughness = clamp(roughness, 0.04500000178813934326171875f, 1.0f);
    float3 normal = normalize(worldNormal);
    if ((flags & 2u) != 0u)
    {
        float3 tangent = normalize(worldTangent.xyz);
        float3 bitangent = normalize(cross(normal, tangent)) * worldTangent.w;
        float2 normalXY = (normalMap.Sample(_normalMap_sampler, uv).xy * 2.0f) - 1.0f.xx;
        float3 mapped = float3(normalXY, sqrt(max(1.0f - dot(normalXY, normalXY), 0.0f)));
        normal = normalize(mul(mapped, float3x3(float3(tangent), float3(bitangent), float3(normal))));
    }
    float ao = material_RoughnessAoAlphaCutoff.y;
    if ((flags & 64u) != 0u)
    {
        ao *= metallicRoughnessMap.Sample(_metallicRoughnessMap_sampler, uv).z;
    }
    if ((flags & 8u) != 0u)
    {
        ao *= occlusionMap.Sample(_occlusionMap_sampler, uv).x;
    }
    float3 emissive = material_EmissiveMetallic.xyz;
    if ((flags & 16u) != 0u)
    {
        emissive *= emissiveMap.Sample(_emissiveMap_sampler, uv).xyz;
    }
    float3 viewDirection = normalize(frame_CameraPosition.xyz - worldPosition);
    float3 lightDirection = normalize(-frame_SunDirectionIntensity.xyz);
    float nDotV = max(dot(normal, viewDirection), 0.0f);
    float nDotL = max(dot(normal, lightDirection), 0.0f);
    float3 f0 = lerp(material_RoughnessAoAlphaCutoff.w.xxx, baseColor.xyz, metallic.xxx);
    int activeCascade = 0;
    float _1165;
    if ((frame_ShadowParameters.y > 0.5f) && (nDotL > 0.0f))
    {
        float3 param = worldPosition;
        float param_1 = nDotL;
        int param_2;
        float _1173 = SampleSunShadow(param, param_1, param_2);
        activeCascade = param_2;
        _1165 = _1173;
    }
    else
    {
        _1165 = 1.0f;
    }
    float sunVisibility = _1165;
    float3 param_3 = normal;
    float3 param_4 = viewDirection;
    float3 param_5 = lightDirection;
    float3 param_6 = frame_SunColor.xyz * frame_SunDirectionIntensity.w;
    float3 param_7 = baseColor.xyz;
    float3 param_8 = f0;
    float param_9 = roughness;
    float param_10 = metallic;
    float3 direct = EvaluateLight(param_3, param_4, param_5, param_6, param_7, param_8, param_9, param_10) * sunVisibility;
    for (uint i = 0u; i < min(frame_LightCounts.x, 8u); i++)
    {
        float3 toLight = frame_PointPositionRadius[i].xyz - worldPosition;
        float distanceToLight = length(toLight);
        float3 localDirection = toLight / max(distanceToLight, 9.9999997473787516355514526367188e-05f).xxx;
        float ratio = clamp(distanceToLight / frame_PointPositionRadius[i].w, 0.0f, 1.0f);
        float window = 1.0f - (((ratio * ratio) * ratio) * ratio);
        float attenuation = (window * window) / ((distanceToLight * distanceToLight) + 1.0f);
        float3 param_11 = normal;
        float3 param_12 = viewDirection;
        float3 param_13 = localDirection;
        float3 param_14 = frame_PointColor[i].xyz;
        float3 param_15 = baseColor.xyz;
        float3 param_16 = f0;
        float param_17 = roughness;
        float param_18 = metallic;
        uint param_19 = i;
        float3 param_20 = worldPosition;
        uint param_21 = i;
        float3 param_22 = worldPosition;
        direct += (((EvaluateLight(param_11, param_12, param_13, param_14, param_15, param_16, param_17, param_18) * attenuation) * SamplePointShadow(param_19, param_20)) * SamplePointCookie(param_21, param_22));
    }
    float _1377;
    float2 param_29;
    float param_30;
    float _1411;
    for (uint i_1 = 0u; i_1 < min(frame_LightCounts.y, 4u); i_1++)
    {
        float3 toLight_1 = frame_SpotPositionRange[i_1].xyz - worldPosition;
        float distanceToLight_1 = length(toLight_1);
        float3 localDirection_1 = toLight_1 / max(distanceToLight_1, 9.9999997473787516355514526367188e-05f).xxx;
        float coneAngle = dot(-localDirection_1, frame_SpotDirectionCosOuter[i_1].xyz);
        float cone = smoothstep(frame_SpotDirectionCosOuter[i_1].w, frame_SpotColorCosInner[i_1].w, coneAngle);
        float ratio_1 = clamp(distanceToLight_1 / frame_SpotPositionRange[i_1].w, 0.0f, 1.0f);
        float window_1 = 1.0f - (((ratio_1 * ratio_1) * ratio_1) * ratio_1);
        float attenuation_1 = (window_1 * window_1) / ((distanceToLight_1 * distanceToLight_1) + 1.0f);
        float localNdotL = max(dot(normal, localDirection_1), 0.0f);
        if (localNdotL > 0.0f)
        {
            float4x4 param_23 = frame_SpotMatrices[i_1];
            float4 param_24 = frame_SpotShadowRects[i_1];
            float3 param_25 = worldPosition;
            float param_26 = localNdotL;
            _1377 = SampleProjectedShadow(param_23, param_24, param_25, param_26);
        }
        else
        {
            _1377 = 1.0f;
        }
        float shadow = _1377;
        float4x4 param_27 = frame_SpotMatrices[i_1];
        float3 param_28 = worldPosition;
        bool _1408 = ProjectLocalLight(param_27, param_28, param_29, param_30);
        float2 projectedUv = param_29;
        float projectedDepth = param_30;
        if (_1408)
        {
            int param_31 = int(round(frame_SpotCookieData[i_1].x));
            float2 param_32 = projectedUv;
            _1411 = SampleCookie(param_31, param_32);
        }
        else
        {
            _1411 = 0.0f;
        }
        float cookie = _1411;
        float3 param_33 = normal;
        float3 param_34 = viewDirection;
        float3 param_35 = localDirection_1;
        float3 param_36 = frame_SpotColorCosInner[i_1].xyz;
        float3 param_37 = baseColor.xyz;
        float3 param_38 = f0;
        float param_39 = roughness;
        float param_40 = metallic;
        direct += ((((EvaluateLight(param_33, param_34, param_35, param_36, param_37, param_38, param_39, param_40) * attenuation_1) * cone) * shadow) * cookie);
    }
    float2 param_43;
    float param_44;
    float _1635;
    for (uint i_2 = 0u; i_2 < min(frame_LightCounts.z, 4u); i_2++)
    {
        float3 fromCenter = worldPosition - frame_AreaPositionRange[i_2].xyz;
        float3 closest = (frame_AreaPositionRange[i_2].xyz + (frame_AreaRightHalfWidth[i_2].xyz * clamp(dot(fromCenter, frame_AreaRightHalfWidth[i_2].xyz), -frame_AreaRightHalfWidth[i_2].w, frame_AreaRightHalfWidth[i_2].w))) + (frame_AreaUpHalfHeight[i_2].xyz * clamp(dot(fromCenter, frame_AreaUpHalfHeight[i_2].xyz), -frame_AreaUpHalfHeight[i_2].w, frame_AreaUpHalfHeight[i_2].w));
        float3 toLight_2 = closest - worldPosition;
        float distanceToLight_2 = length(toLight_2);
        float3 localDirection_2 = toLight_2 / max(distanceToLight_2, 9.9999997473787516355514526367188e-05f).xxx;
        float facing = max(dot(-localDirection_2, frame_AreaDirectionMinRoughness[i_2].xyz), 0.0f);
        float ratio_2 = clamp(distanceToLight_2 / frame_AreaPositionRange[i_2].w, 0.0f, 1.0f);
        float window_2 = 1.0f - (((ratio_2 * ratio_2) * ratio_2) * ratio_2);
        float attenuation_2 = (window_2 * window_2) / ((distanceToLight_2 * distanceToLight_2) + 1.0f);
        float4x4 param_41 = frame_AreaMatrices[i_2];
        float3 param_42 = worldPosition;
        bool _1584 = ProjectLocalLight(param_41, param_42, param_43, param_44);
        float2 projectedUv_1 = param_43;
        float projectedDepth_1 = param_44;
        if (!_1584)
        {
            continue;
        }
        float2 centered = abs(projectedUv_1 - 0.5f.xx);
        float2 softness = frame_AreaSoftness[i_2].xy;
        float2 edge = 1.0f.xx - smoothstep(0.5f.xx - (softness * 0.5f), 0.5f.xx, centered);
        float barn = edge.x * edge.y;
        int param_45 = int(round(frame_AreaCookieData[i_2].x));
        float2 param_46 = projectedUv_1;
        float cookie_1 = SampleCookie(param_45, param_46);
        float localNdotL_1 = max(dot(normal, localDirection_2), 0.0f);
        if (localNdotL_1 > 0.0f)
        {
            float4x4 param_47 = frame_AreaMatrices[i_2];
            float4 param_48 = frame_AreaShadowRects[i_2];
            float3 param_49 = worldPosition;
            float param_50 = localNdotL_1;
            _1635 = SampleProjectedShadow(param_47, param_48, param_49, param_50);
        }
        else
        {
            _1635 = 1.0f;
        }
        float shadow_1 = _1635;
        float3 param_51 = normal;
        float3 param_52 = viewDirection;
        float3 param_53 = localDirection_2;
        float3 param_54 = frame_AreaColor[i_2].xyz;
        float3 param_55 = baseColor.xyz;
        float3 param_56 = f0;
        float param_57 = max(roughness, frame_AreaDirectionMinRoughness[i_2].w);
        float param_58 = metallic;
        direct += (((((EvaluateLight(param_51, param_52, param_53, param_54, param_55, param_56, param_57, param_58) * attenuation_2) * facing) * barn) * cookie_1) * shadow_1);
    }
    float param_59 = nDotV;
    float3 param_60 = f0;
    float param_61 = roughness;
    float3 ambientFresnel = F_SchlickRoughness(param_59, param_60, param_61);
    float3 reflection = reflect(-viewDirection, normal);
    float3 diffuseEnvironment = irradianceMap.Sample(_irradianceMap_sampler, normal).xyz;
    float3 diffuseAmbient = (((1.0f.xxx - ambientFresnel) * (1.0f - metallic)) * diffuseEnvironment) * baseColor.xyz;
    float3 specularEnvironment = prefilteredMap.SampleLevel(_prefilteredMap_sampler, reflection, roughness * 7.0f).xyz;
    float2 integratedBrdf = brdfLut.Sample(_brdfLut_sampler, float2(nDotV, roughness)).xy;
    float3 specularAmbient = specularEnvironment * ((f0 * integratedBrdf.x) + integratedBrdf.y.xxx);
    float3 color = (((diffuseAmbient + specularAmbient) * ao) + direct) + emissive;
    if (frame_ShadowParameters.z > 0.5f)
    {
        color = lerp(color, color * _1781[activeCascade], 0.550000011920928955078125f.xxx);
    }
    if (frame_FogHeightEnabled.z > 0.5f)
    {
        float distanceToCamera = length(worldPosition - frame_CameraPosition.xyz);
        float distanceFog = pow(clamp((distanceToCamera - frame_FogStartEndExponents.x) / max(frame_FogStartEndExponents.y - frame_FogStartEndExponents.x, 9.9999997473787516355514526367188e-05f), 0.0f, 1.0f), frame_FogStartEndExponents.z);
        float heightFog = pow(clamp((frame_FogHeightEnabled.x - worldPosition.y) / max(frame_FogHeightEnabled.x - frame_FogHeightEnabled.y, 9.9999997473787516355514526367188e-05f), 0.0f, 1.0f), frame_FogStartEndExponents.w);
        color = lerp(color, frame_FogColorOpacity.xyz, ((distanceFog * heightFog) * frame_FogColorOpacity.w).xxx);
    }
    float2 currentUv = ((currentClip.xy / max(currentClip.w, 9.9999999747524270787835121154785e-07f).xx) * 0.5f) + 0.5f.xx;
    float2 previousUv = ((previousClip.xy / max(previousClip.w, 9.9999999747524270787835121154785e-07f).xx) * 0.5f) + 0.5f.xx;
    outVelocity = currentUv - previousUv;
    outColor = float4(color, baseColor.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    uv = stage_input.uv;
    worldNormal = stage_input.worldNormal;
    worldTangent = stage_input.worldTangent;
    worldPosition = stage_input.worldPosition;
    currentClip = stage_input.currentClip;
    previousClip = stage_input.previousClip;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.outVelocity = outVelocity;
    stage_output.outColor = outColor;
    return stage_output;
}
