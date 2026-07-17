struct PointLight
{
    float3 Position;
    float3 Color;
    float Radius;
    int ShadowIndex;
    int CookieIndex;
};

struct SpotLight
{
    float3 Position;
    float3 Direction;
    float3 Color;
    float Range;
    float CosInner;
    float CosOuter;
    row_major float4x4 Matrix;
    float4 ShadowRect;
    int CookieIndex;
};

struct AreaLight
{
    float3 Position;
    float3 Direction;
    float3 Right;
    float3 Up;
    float3 Color;
    float2 HalfSize;
    float2 Softness;
    float Range;
    float MinRoughness;
    row_major float4x4 Matrix;
    float4 ShadowRect;
    int CookieIndex;
};

static const float3 _765[8] = { 1.0f.xxx, float3(-1.0f, 1.0f, 1.0f), float3(1.0f, -1.0f, 1.0f), float3(-1.0f, -1.0f, 1.0f), float3(1.0f, 1.0f, -1.0f), float3(-1.0f, 1.0f, -1.0f), float3(1.0f, -1.0f, -1.0f), (-1.0f).xxx };
static const float3 _1713[4] = { float3(1.0f, 0.25f, 0.25f), float3(0.25f, 1.0f, 0.25f), float3(0.25f, 0.449999988079071044921875f, 1.0f), float3(1.0f, 0.85000002384185791015625f, 0.20000000298023223876953125f) };

cbuffer EngineGlobals_lighting_pbr_frag_hlsl : register(b13)
{
    row_major float4x4 uCascadeMatrices[4];
    row_major float4x4 uView;
    float uCascadeSplits[4];
    float uCascadeBlendFraction;
    bool uFogEnabled;
    float3 uCameraPos;
    float2 uFogStartEnd;
    float uFogDistanceExponent;
    float2 uFogHeightTopBottom;
    float uFogHeightExponent;
    float uFogOpacity;
    float3 uFogColor;
    bool uHasNormalMap;
    float3 uAlbedo;
    float uBaseColorAlpha;
    bool uHasAlbedoMap;
    bool uAlphaMasked;
    float uAlphaCutoff;
    float uMetallic;
    float uRoughness;
    float uAO;
    bool uHasMraoMap;
    bool uHasMetallicRoughnessMap;
    bool uHasOcclusionMap;
    float uSpecularF0;
    float3 uSunDirection;
    bool uSunCastsShadows;
    float3 uSunColor;
    int uPointLightCount;
    PointLight uPointLights[8];
    int uSpotLightCount;
    SpotLight uSpotLights[4];
    int uAreaLightCount;
    AreaLight uAreaLights[4];
    float uPrefilterMips;
    float3 uEmissive;
    bool uHasEmissiveMap;
    bool uDebugCascades;
};

Texture2D<float4> uShadowMaps[4] : register(t8);
SamplerState _uShadowMaps_sampler[4] : register(s8);
Texture2D<float4> uLightCookieAtlas : register(t14);
SamplerState _uLightCookieAtlas_sampler : register(s14);
Texture2D<float4> uLocalShadowAtlas : register(t12);
SamplerState _uLocalShadowAtlas_sampler : register(s12);
TextureCubeArray<float4> uPointShadowMaps : register(t13);
SamplerState _uPointShadowMaps_sampler : register(s13);
Texture2D<float4> uNormalMap : register(t1);
SamplerState _uNormalMap_sampler : register(s1);
Texture2D<float4> uAlbedoMap : register(t0);
SamplerState _uAlbedoMap_sampler : register(s0);
Texture2D<float4> uMraoMap : register(t2);
SamplerState _uMraoMap_sampler : register(s2);
Texture2D<float4> uMetallicRoughnessMap : register(t15);
SamplerState _uMetallicRoughnessMap_sampler : register(s15);
Texture2D<float4> uOcclusionMap : register(t4);
SamplerState _uOcclusionMap_sampler : register(s4);
TextureCube<float4> uIrradianceMap : register(t5);
SamplerState _uIrradianceMap_sampler : register(s5);
TextureCube<float4> uPrefilterMap : register(t6);
SamplerState _uPrefilterMap_sampler : register(s6);
Texture2D<float4> uBrdfLut : register(t7);
SamplerState _uBrdfLut_sampler : register(s7);
Texture2D<float4> uEmissiveMap : register(t3);
SamplerState _uEmissiveMap_sampler : register(s3);

static float3 vNormal;
static float4 vTangent;
static float2 vUV;
static float3 vWorldPos;
static float4 FragColor;
static float4 vCurrentClip;
static float4 vPreviousClip;
static float2 OutVelocity;

struct SPIRV_Cross_Input
{
    float3 vWorldPos : TEXCOORD0;
    float3 vNormal : TEXCOORD1;
    float4 vTangent : TEXCOORD2;
    float2 vUV : TEXCOORD3;
    float4 vCurrentClip : TEXCOORD4;
    float4 vPreviousClip : TEXCOORD5;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
    float2 OutVelocity : SV_Target1;
};

uint2 spvTextureSize(Texture2D<float4> Tex, uint Level, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(Level, ret.x, ret.y, Param);
    return ret;
}

float SampleCascadeDepth(int cascade, float2 uv)
{
    // Shader Model 5.0 requires literal sampler-array indices. Keeping this
    // small explicit switch also produces predictable OpenGL SPIR-V while
    // preserving the existing four independently bound cascade textures.
    switch (cascade)
    {
    case 0: return uShadowMaps[0].Sample(_uShadowMaps_sampler[0], uv).x;
    case 1: return uShadowMaps[1].Sample(_uShadowMaps_sampler[1], uv).x;
    case 2: return uShadowMaps[2].Sample(_uShadowMaps_sampler[2], uv).x;
    default: return uShadowMaps[3].Sample(_uShadowMaps_sampler[3], uv).x;
    }
}

float SampleCascadeShadow(int cascade, float3 worldPos, float NoL)
{
    float4 lightSpacePos = mul(float4(worldPos, 1.0f), uCascadeMatrices[cascade]);
    float3 proj = lightSpacePos.xyz / lightSpacePos.w.xxx;
    proj = (proj * 0.5f) + 0.5f.xxx;
    bool _293 = proj.z > 1.0f;
    bool _301;
    if (!_293)
    {
        _301 = proj.x < 0.0f;
    }
    else
    {
        _301 = _293;
    }
    bool _308;
    if (!_301)
    {
        _308 = proj.x > 1.0f;
    }
    else
    {
        _308 = _301;
    }
    bool _316;
    if (!_308)
    {
        _316 = proj.y < 0.0f;
    }
    else
    {
        _316 = _308;
    }
    bool _323;
    if (!_316)
    {
        _323 = proj.y > 1.0f;
    }
    else
    {
        _323 = _316;
    }
    if (_323)
    {
        return 1.0f;
    }
    float bias = max(0.0024999999441206455230712890625f * (1.0f - NoL), 0.0005000000237487256526947021484375f);
    float shadow = 0.0f;
    uint _348_dummy_parameter;
    float2 texel = 1.0f.xx / float2(int2(spvTextureSize(uShadowMaps[0], uint(0), _348_dummy_parameter)));
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float closestDepth = SampleCascadeDepth(
                cascade, proj.xy + (float2(float(x), float(y)) * texel));
            shadow += (((proj.z - bias) > closestDepth) ? 0.0f : 1.0f);
        }
    }
    return shadow / 9.0f;
}

float SampleCascadedShadow(float3 worldPos, float NoL, inout int cascadeIndex)
{
    float viewDepth = -mul(float4(worldPos, 1.0f), uView).z;
    cascadeIndex = 3;
    for (int i = 0; i < 4; i++)
    {
        if (viewDepth <= uCascadeSplits[i])
        {
            cascadeIndex = i;
            break;
        }
    }
    if (viewDepth > uCascadeSplits[3])
    {
        return 1.0f;
    }
    int param = cascadeIndex;
    float3 param_1 = worldPos;
    float param_2 = NoL;
    float visibility = SampleCascadeShadow(param, param_1, param_2);
    if (cascadeIndex < 3)
    {
        float _462;
        if (cascadeIndex == 0)
        {
            _462 = 0.0f;
        }
        else
        {
            _462 = uCascadeSplits[cascadeIndex - 1];
        }
        float cascadeNear = _462;
        float blendWidth = (uCascadeSplits[cascadeIndex] - cascadeNear) * uCascadeBlendFraction;
        float blendStart = uCascadeSplits[cascadeIndex] - blendWidth;
        if (viewDepth > blendStart)
        {
            int param_3 = cascadeIndex + 1;
            float3 param_4 = worldPos;
            float param_5 = NoL;
            float nextVisibility = SampleCascadeShadow(param_3, param_4, param_5);
            visibility = lerp(visibility, nextVisibility, smoothstep(blendStart, uCascadeSplits[cascadeIndex], viewDepth));
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

float SamplePointShadow(PointLight light, float3 worldPos)
{
    if (light.ShadowIndex < 0)
    {
        return 1.0f;
    }
    float3 fromLight = worldPos - light.Position;
    float distanceToLight = length(fromLight);
    float3 direction = fromLight / max(distanceToLight, 9.9999997473787516355514526367188e-05f).xxx;
    float disk = 0.0030000000260770320892333984375f + ((0.0199999995529651641845703125f * distanceToLight) / light.Radius);
    float visibility = 0.0f;
    for (int i = 0; i < 8; i++)
    {
        float stored = uPointShadowMaps.Sample(_uPointShadowMaps_sampler, float4(direction + (_765[i] * disk), float(light.ShadowIndex))).x * light.Radius;
        visibility += float((distanceToLight - 0.0350000001490116119384765625f) <= stored);
    }
    return visibility / 8.0f;
}

float SampleCookie(int slot, float2 uv)
{
    if (slot <= 0)
    {
        return 1.0f;
    }
    float2 offset = float2(float(slot % 4), float(slot / 4)) * 0.25f;
    return uLightCookieAtlas.Sample(_uLightCookieAtlas_sampler, offset + (clamp(uv, 0.0f.xx, 1.0f.xx) * 0.25f)).x;
}

float SamplePointCookie(PointLight light, float3 worldPos)
{
    if (light.CookieIndex <= 0)
    {
        return 1.0f;
    }
    float3 direction = normalize(worldPos - light.Position);
    float2 uv = float2((atan2(direction.z, direction.x) / 6.283185482025146484375f) + 0.5f, (asin(clamp(direction.y, -1.0f, 1.0f)) / 3.1415927410125732421875f) + 0.5f);
    int param = light.CookieIndex;
    float2 param_1 = uv;
    return SampleCookie(param, param_1);
}

bool ProjectLocalLight(float4x4 _matrix, float3 worldPos, inout float2 uv, inout float depth)
{
    float4 lightSpacePos = mul(float4(worldPos, 1.0f), _matrix);
    if (lightSpacePos.w <= 0.0f)
    {
        return false;
    }
    float3 proj = lightSpacePos.xyz / lightSpacePos.w.xxx;
    proj = (proj * 0.5f) + 0.5f.xxx;
    uv = proj.xy;
    depth = proj.z;
    bool _572 = proj.z >= 0.0f;
    bool _578;
    if (_572)
    {
        _578 = proj.z <= 1.0f;
    }
    else
    {
        _578 = _572;
    }
    bool _587;
    if (_578)
    {
        _587 = all(bool2(proj.xy.x >= 0.0f.xx.x, proj.xy.y >= 0.0f.xx.y));
    }
    else
    {
        _587 = _578;
    }
    bool _595;
    if (_587)
    {
        _595 = all(bool2(proj.xy.x <= 1.0f.xx.x, proj.xy.y <= 1.0f.xx.y));
    }
    else
    {
        _595 = _587;
    }
    return _595;
}

float SampleProjectedShadow(float4x4 _matrix, float4 atlasRect, float3 worldPos, float NoL)
{
    if (atlasRect.z <= 0.0f)
    {
        return 1.0f;
    }
    float4x4 param = _matrix;
    float3 param_1 = worldPos;
    float2 param_2;
    float param_3;
    bool _612 = ProjectLocalLight(param, param_1, param_2, param_3);
    float2 uv = param_2;
    float depth = param_3;
    if (!_612)
    {
        return 1.0f;
    }
    float bias = max(0.00200000009499490261077880859375f * (1.0f - NoL), 0.00039999998989515006542205810546875f);
    float shadow = 0.0f;
    uint _631_dummy_parameter;
    float2 texel = 1.0f.xx / float2(int2(spvTextureSize(uLocalShadowAtlas, uint(0), _631_dummy_parameter)));
    float2 atlasUv = atlasRect.xy + (uv * atlasRect.zw);
    float2 atlasMin = atlasRect.xy + (texel * 0.5f);
    float2 atlasMax = (atlasRect.xy + atlasRect.zw) - (texel * 0.5f);
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float2 sampleUv = clamp(atlasUv + (float2(float(x), float(y)) * texel), atlasMin, atlasMax);
            float closestDepth = uLocalShadowAtlas.Sample(_uLocalShadowAtlas_sampler, sampleUv).x;
            shadow += (((depth - bias) > closestDepth) ? 0.0f : 1.0f);
        }
    }
    return shadow / 9.0f;
}

float3 F_SchlickRoughness(float cosine, float3 f0, float roughness)
{
    return f0 + ((max((1.0f - roughness).xxx, f0) - f0) * pow(clamp(1.0f - cosine, 0.0f, 1.0f), 5.0f));
}

void ApplyGradientFog(inout float3 color, float3 worldPos)
{
    if (!uFogEnabled)
    {
        return;
    }
    float dist = length(worldPos - uCameraPos);
    float distFactor = pow(clamp((dist - uFogStartEnd.x) / max(uFogStartEnd.y - uFogStartEnd.x, 9.9999997473787516355514526367188e-05f), 0.0f, 1.0f), uFogDistanceExponent);
    float heightFactor = pow(clamp((uFogHeightTopBottom.x - worldPos.y) / max(uFogHeightTopBottom.x - uFogHeightTopBottom.y, 9.9999997473787516355514526367188e-05f), 0.0f, 1.0f), uFogHeightExponent);
    float blend = (distFactor * heightFactor) * uFogOpacity;
    color = lerp(color, uFogColor, blend.xxx);
}

void frag_main()
{
    float3 geoNormal = normalize(vNormal);
    float3 N = geoNormal;
    if (uHasNormalMap)
    {
        float3 T = normalize(vTangent.xyz - (geoNormal * dot(vTangent.xyz, geoNormal)));
        float3 B = cross(geoNormal, T) * vTangent.w;
        float2 normalXY = (uNormalMap.Sample(_uNormalMap_sampler, vUV).xy * 2.0f) - 1.0f.xx;
        float3 texNormal = float3(normalXY, sqrt(max(1.0f - dot(normalXY, normalXY), 0.0f)));
        N = normalize(mul(texNormal, float3x3(float3(T), float3(B), float3(geoNormal))));
    }
    float3 V = normalize(uCameraPos - vWorldPos);
    float4 baseColor = float4(uAlbedo, uBaseColorAlpha);
    if (uHasAlbedoMap)
    {
        baseColor *= uAlbedoMap.Sample(_uAlbedoMap_sampler, vUV);
    }
    bool _1009;
    if (uAlphaMasked)
    {
        _1009 = baseColor.w < uAlphaCutoff;
    }
    else
    {
        _1009 = uAlphaMasked;
    }
    if (_1009)
    {
        discard;
    }
    float3 albedo = baseColor.xyz;
    float metallic = uMetallic;
    float roughness = uRoughness;
    float ao = uAO;
    if (uHasMraoMap)
    {
        float3 mrao = uMraoMap.Sample(_uMraoMap_sampler, vUV).xyz;
        metallic *= mrao.x;
        roughness *= mrao.y;
        ao *= mrao.z;
    }
    if (uHasMetallicRoughnessMap)
    {
        float3 mr = uMetallicRoughnessMap.Sample(_uMetallicRoughnessMap_sampler, vUV).xyz;
        roughness *= mr.y;
        metallic *= mr.z;
    }
    if (uHasOcclusionMap)
    {
        ao *= uOcclusionMap.Sample(_uOcclusionMap_sampler, vUV).x;
    }
    float3 F0 = lerp(uSpecularF0.xxx, albedo, metallic.xxx);
    roughness = clamp(roughness, 0.04500000178813934326171875f, 1.0f);
    float3 L0 = 0.0f.xxx;
    int activeCascade = 0;
    float3 L = normalize(-uSunDirection);
    float NoL = max(dot(N, L), 0.0f);
    float _1106;
    if (uSunCastsShadows && (NoL > 0.0f))
    {
        float3 param = vWorldPos;
        float param_1 = NoL;
        int param_2;
        float _1114 = SampleCascadedShadow(param, param_1, param_2);
        activeCascade = param_2;
        _1106 = _1114;
    }
    else
    {
        _1106 = 1.0f;
    }
    float shadow = _1106;
    float3 param_3 = N;
    float3 param_4 = V;
    float3 param_5 = L;
    float3 param_6 = uSunColor;
    float3 param_7 = albedo;
    float3 param_8 = F0;
    float param_9 = roughness;
    float param_10 = metallic;
    L0 += (EvaluateLight(param_3, param_4, param_5, param_6, param_7, param_8, param_9, param_10) * shadow);
    for (int i = 0; i < uPointLightCount; i++)
    {
        float3 toLight = uPointLights[i].Position - vWorldPos;
        float dist = length(toLight);
        float3 L_1 = toLight / max(dist, 9.9999997473787516355514526367188e-05f).xxx;
        float distRatio = clamp(dist / uPointLights[i].Radius, 0.0f, 1.0f);
        float window = 1.0f - (((distRatio * distRatio) * distRatio) * distRatio);
        float attenuation = (window * window) / ((dist * dist) + 1.0f);
        PointLight param_11 = uPointLights[i];
        float3 param_12 = vWorldPos;
        float shadow_1 = SamplePointShadow(param_11, param_12);
        PointLight param_13 = uPointLights[i];
        float3 param_14 = vWorldPos;
        float cookie = SamplePointCookie(param_13, param_14);
        float3 param_15 = N;
        float3 param_16 = V;
        float3 param_17 = L_1;
        float3 param_18 = uPointLights[i].Color;
        float3 param_19 = albedo;
        float3 param_20 = F0;
        float param_21 = roughness;
        float param_22 = metallic;
        L0 += (((EvaluateLight(param_15, param_16, param_17, param_18, param_19, param_20, param_21, param_22) * attenuation) * shadow_1) * cookie);
    }
    float _1324;
    float2 param_29;
    float param_30;
    float _1359;
    for (int i_1 = 0; i_1 < uSpotLightCount; i_1++)
    {
        float3 toLight_1 = uSpotLights[i_1].Position - vWorldPos;
        float dist_1 = length(toLight_1);
        float3 L_2 = toLight_1 / max(dist_1, 9.9999997473787516355514526367188e-05f).xxx;
        float cosAngle = dot(-L_2, uSpotLights[i_1].Direction);
        float cone = smoothstep(uSpotLights[i_1].CosOuter, uSpotLights[i_1].CosInner, cosAngle);
        if (cone <= 0.0f)
        {
            continue;
        }
        float distRatio_1 = clamp(dist_1 / uSpotLights[i_1].Range, 0.0f, 1.0f);
        float window_1 = 1.0f - (((distRatio_1 * distRatio_1) * distRatio_1) * distRatio_1);
        float attenuation_1 = (window_1 * window_1) / ((dist_1 * dist_1) + 1.0f);
        float NoL_1 = max(dot(N, L_2), 0.0f);
        if (NoL_1 > 0.0f)
        {
            float4x4 param_23 = uSpotLights[i_1].Matrix;
            float4 param_24 = uSpotLights[i_1].ShadowRect;
            float3 param_25 = vWorldPos;
            float param_26 = NoL_1;
            _1324 = SampleProjectedShadow(param_23, param_24, param_25, param_26);
        }
        else
        {
            _1324 = 1.0f;
        }
        float shadow_2 = _1324;
        float4x4 param_27 = uSpotLights[i_1].Matrix;
        float3 param_28 = vWorldPos;
        bool _1356 = ProjectLocalLight(param_27, param_28, param_29, param_30);
        float2 projectedUv = param_29;
        float projectedDepth = param_30;
        if (_1356)
        {
            int param_31 = uSpotLights[i_1].CookieIndex;
            float2 param_32 = projectedUv;
            _1359 = SampleCookie(param_31, param_32);
        }
        else
        {
            _1359 = 0.0f;
        }
        float cookie_1 = _1359;
        float3 param_33 = N;
        float3 param_34 = V;
        float3 param_35 = L_2;
        float3 param_36 = uSpotLights[i_1].Color;
        float3 param_37 = albedo;
        float3 param_38 = F0;
        float param_39 = roughness;
        float param_40 = metallic;
        L0 += ((((EvaluateLight(param_33, param_34, param_35, param_36, param_37, param_38, param_39, param_40) * attenuation_1) * cone) * shadow_2) * cookie_1);
    }
    float2 param_43;
    float param_44;
    float _1550;
    for (int i_2 = 0; i_2 < uAreaLightCount; i_2++)
    {
        AreaLight light = uAreaLights[i_2];
        float3 fromCenter = vWorldPos - light.Position;
        float3 closest = (light.Position + (light.Right * clamp(dot(fromCenter, light.Right), -light.HalfSize.x, light.HalfSize.x))) + (light.Up * clamp(dot(fromCenter, light.Up), -light.HalfSize.y, light.HalfSize.y));
        float3 toLight_2 = closest - vWorldPos;
        float dist_2 = length(toLight_2);
        float3 L_3 = toLight_2 / max(dist_2, 9.9999997473787516355514526367188e-05f).xxx;
        float facing = max(dot(-L_3, light.Direction), 0.0f);
        float distRatio_2 = clamp(dist_2 / light.Range, 0.0f, 1.0f);
        float window_2 = 1.0f - pow(distRatio_2, 4.0f);
        float attenuation_2 = (window_2 * window_2) / ((dist_2 * dist_2) + 1.0f);
        float4x4 param_41 = light.Matrix;
        float3 param_42 = vWorldPos;
        bool _1507 = ProjectLocalLight(param_41, param_42, param_43, param_44);
        float2 projectedUv_1 = param_43;
        float projectedDepth_1 = param_44;
        if (!_1507)
        {
            continue;
        }
        float2 centered = abs(projectedUv_1 - 0.5f.xx);
        float2 edge = 1.0f.xx - smoothstep(0.5f.xx - (light.Softness * 0.5f), 0.5f.xx, centered);
        float barn = edge.x * edge.y;
        int param_45 = light.CookieIndex;
        float2 param_46 = projectedUv_1;
        float cookie_2 = SampleCookie(param_45, param_46);
        float NoL_2 = max(dot(N, L_3), 0.0f);
        if (NoL_2 > 0.0f)
        {
            float4x4 param_47 = light.Matrix;
            float4 param_48 = light.ShadowRect;
            float3 param_49 = vWorldPos;
            float param_50 = NoL_2;
            _1550 = SampleProjectedShadow(param_47, param_48, param_49, param_50);
        }
        else
        {
            _1550 = 1.0f;
        }
        float shadow_3 = _1550;
        float areaRoughness = max(roughness, light.MinRoughness);
        float3 param_51 = N;
        float3 param_52 = V;
        float3 param_53 = L_3;
        float3 param_54 = light.Color;
        float3 param_55 = albedo;
        float3 param_56 = F0;
        float param_57 = areaRoughness;
        float param_58 = metallic;
        L0 += (((((EvaluateLight(param_51, param_52, param_53, param_54, param_55, param_56, param_57, param_58) * attenuation_2) * facing) * barn) * cookie_2) * shadow_3);
    }
    float NoV = max(dot(N, V), 9.9999997473787516355514526367188e-05f);
    float param_59 = NoV;
    float3 param_60 = F0;
    float param_61 = roughness;
    float3 F_ambient = F_SchlickRoughness(param_59, param_60, param_61);
    float3 kd = (1.0f.xxx - F_ambient) * (1.0f - metallic);
    float3 irradiance = uIrradianceMap.Sample(_uIrradianceMap_sampler, N).xyz;
    float3 diffuseIBL = (kd * irradiance) * albedo;
    float3 R = reflect(-V, N);
    float3 prefiltered = uPrefilterMap.SampleLevel(_uPrefilterMap_sampler, R, roughness * (uPrefilterMips - 1.0f)).xyz;
    float2 lut = uBrdfLut.Sample(_uBrdfLut_sampler, float2(NoV, roughness)).xy;
    float3 specularIBL = prefiltered * ((F0 * lut.x) + lut.y.xxx);
    float3 ambient = (diffuseIBL + specularIBL) * ao;
    float3 emissive = uEmissive;
    if (uHasEmissiveMap)
    {
        emissive *= uEmissiveMap.Sample(_uEmissiveMap_sampler, vUV).xyz;
    }
    float3 color = (L0 + ambient) + emissive;
    if (uDebugCascades)
    {
        color = lerp(color, color * _1713[activeCascade], 0.550000011920928955078125f.xxx);
    }
    float3 param_62 = color;
    float3 param_63 = vWorldPos;
    ApplyGradientFog(param_62, param_63);
    color = param_62;
    FragColor = float4(color, 1.0f);
    float2 currentUv = ((vCurrentClip.xy / max(vCurrentClip.w, 9.9999999747524270787835121154785e-07f).xx) * 0.5f) + 0.5f.xx;
    float2 previousUv = ((vPreviousClip.xy / max(vPreviousClip.w, 9.9999999747524270787835121154785e-07f).xx) * 0.5f) + 0.5f.xx;
    OutVelocity = currentUv - previousUv;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vNormal = stage_input.vNormal;
    vTangent = stage_input.vTangent;
    vUV = stage_input.vUV;
    vWorldPos = stage_input.vWorldPos;
    vCurrentClip = stage_input.vCurrentClip;
    vPreviousClip = stage_input.vPreviousClip;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    stage_output.OutVelocity = OutVelocity;
    return stage_output;
}
