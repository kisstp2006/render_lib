#version 460 core

// Cook-Torrance GGX PBR, following the same D/G/F terms Source 2 uses in
// pbr.slang (D_GGX, G_SchlickSmithGGX, F_Schlick), with split-sum IBL for
// ambient exactly like VRF's environment.slang (irradiance for diffuse,
// prefiltered cubemap + BRDF LUT for specular: F0 * lut.x + lut.y).
// Output is LINEAR HDR - exposure/tonemap/gamma happen in post.frag.

in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUV;
in vec4 vCurrentClip;
in vec4 vPreviousClip;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec2 OutVelocity;

uniform vec3 uCameraPos;
uniform mat4 uView;
uniform vec3 uSunDirection; // points FROM the sun TOWARD the scene
uniform vec3 uSunColor;
uniform bool uSunCastsShadows;
uniform mat4 uCascadeMatrices[4];
uniform float uCascadeSplits[4];
uniform sampler2D uShadowMaps[4];
uniform float uCascadeBlendFraction;
uniform bool uDebugCascades;

struct PointLight
{
    vec3 Position;
    vec3 Color;
    float Radius;
    int ShadowIndex;
    int CookieIndex;
};

uniform int uPointLightCount;
uniform PointLight uPointLights[8];

struct SpotLight
{
    vec3 Position;
    vec3 Direction; // normalized, points away from the light
    vec3 Color;     // premultiplied by intensity
    float Range;
    float CosInner;
    float CosOuter;
    mat4 Matrix;
    vec4 ShadowRect;
    int CookieIndex;
};

uniform int uSpotLightCount;
uniform SpotLight uSpotLights[4];

struct AreaLight
{
    vec3 Position;
    vec3 Direction;
    vec3 Right;
    vec3 Up;
    vec3 Color;
    vec2 HalfSize;
    vec2 Softness;
    float Range;
    float MinRoughness;
    mat4 Matrix;
    vec4 ShadowRect;
    int CookieIndex;
};

uniform int uAreaLightCount;
uniform AreaLight uAreaLights[4];
uniform sampler2D uLocalShadowAtlas;
uniform samplerCubeArray uPointShadowMaps;
uniform sampler2D uLightCookieAtlas;

// Gradient fog, same shape as VRF fog.slang ApplyGradientFog:
// distance ramp ^ exp  *  height ramp ^ exp  *  opacity -> mix to color.
uniform bool uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogOpacity;
uniform vec2 uFogStartEnd;
uniform float uFogDistanceExponent;
uniform vec2 uFogHeightTopBottom;
uniform float uFogHeightExponent;

// Scalar material factors
uniform vec3 uAlbedo;
uniform float uBaseColorAlpha;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform float uAO;
uniform float uSpecularF0;

// Texture maps (MRAO: R=metal, G=roughness, B=AO - Source 2 convention)
uniform bool uHasAlbedoMap;
uniform bool uHasNormalMap;
uniform bool uHasMraoMap;
uniform bool uHasMetallicRoughnessMap;
uniform bool uHasOcclusionMap;
uniform bool uHasEmissiveMap;
uniform bool uAlphaMasked;
uniform float uAlphaCutoff;
uniform sampler2D uAlbedoMap; // unit 1
uniform sampler2D uNormalMap; // unit 2
uniform sampler2D uMraoMap;   // unit 3
uniform sampler2D uMetallicRoughnessMap; // unit 3, glTF: G=roughness, B=metalness
uniform sampler2D uEmissiveMap; // unit 8
uniform sampler2D uOcclusionMap; // unit 9, R=occlusion

uniform samplerCube uIrradianceMap;  // unit 4
uniform samplerCube uPrefilterMap;   // unit 5
uniform sampler2D uBrdfLut;          // unit 6
uniform float uPrefilterMips;

#include "../../common/pbr_brdf.glsl"

float SampleCascadeShadow(int cascade, vec3 worldPos, float NoL)
{
    vec4 lightSpacePos = uCascadeMatrices[cascade] * vec4(worldPos, 1.0);
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    float bias = max(0.0025 * (1.0 - NoL), 0.0005);
    float shadow = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMaps[cascade], 0));

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float closestDepth = texture(uShadowMaps[cascade], proj.xy + vec2(x, y) * texel).r;
            shadow += (proj.z - bias) > closestDepth ? 0.0 : 1.0;
        }
    }

    return shadow / 9.0;
}

float SampleCascadedShadow(vec3 worldPos, float NoL, out int cascadeIndex)
{
    float viewDepth = -(uView * vec4(worldPos, 1.0)).z;
    cascadeIndex = 3;
    for (int i = 0; i < 4; ++i)
    {
        if (viewDepth <= uCascadeSplits[i])
        {
            cascadeIndex = i;
            break;
        }
    }
    if (viewDepth > uCascadeSplits[3])
        return 1.0;

    float visibility = SampleCascadeShadow(cascadeIndex, worldPos, NoL);
    if (cascadeIndex < 3)
    {
        float cascadeNear = cascadeIndex == 0 ? 0.0 : uCascadeSplits[cascadeIndex - 1];
        float blendWidth = (uCascadeSplits[cascadeIndex] - cascadeNear) * uCascadeBlendFraction;
        float blendStart = uCascadeSplits[cascadeIndex] - blendWidth;
        if (viewDepth > blendStart)
        {
            float nextVisibility = SampleCascadeShadow(cascadeIndex + 1, worldPos, NoL);
            visibility = mix(visibility, nextVisibility,
                             smoothstep(blendStart, uCascadeSplits[cascadeIndex], viewDepth));
        }
    }
    return visibility;
}

float SampleCookie(int slot, vec2 uv)
{
    if (slot <= 0)
        return 1.0;
    const float scale = 0.25;
    vec2 offset = vec2(slot % 4, slot / 4) * scale;
    return texture(uLightCookieAtlas, offset + clamp(uv, 0.0, 1.0) * scale).r;
}

bool ProjectLocalLight(mat4 matrix, vec3 worldPos, out vec2 uv, out float depth)
{
    vec4 lightSpacePos = matrix * vec4(worldPos, 1.0);
    if (lightSpacePos.w <= 0.0)
        return false;

    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    uv = proj.xy;
    depth = proj.z;
    return proj.z >= 0.0 && proj.z <= 1.0 && all(greaterThanEqual(proj.xy, vec2(0.0))) && all(lessThanEqual(proj.xy, vec2(1.0)));
}

float SampleProjectedShadow(mat4 matrix, vec4 atlasRect, vec3 worldPos, float NoL)
{
    if (atlasRect.z <= 0.0)
        return 1.0;
    vec2 uv;
    float depth;
    if (!ProjectLocalLight(matrix, worldPos, uv, depth))
        return 1.0;

    float bias = max(0.002 * (1.0 - NoL), 0.0004);
    float shadow = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uLocalShadowAtlas, 0));
    vec2 atlasUv = atlasRect.xy + uv * atlasRect.zw;
    vec2 atlasMin = atlasRect.xy + texel * 0.5;
    vec2 atlasMax = atlasRect.xy + atlasRect.zw - texel * 0.5;

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            vec2 sampleUv = clamp(atlasUv + vec2(x, y) * texel, atlasMin, atlasMax);
            float closestDepth = texture(uLocalShadowAtlas, sampleUv).r;
            shadow += (depth - bias) > closestDepth ? 0.0 : 1.0;
        }
    }

    return shadow / 9.0;
}

float SamplePointShadow(PointLight light, vec3 worldPos)
{
    if (light.ShadowIndex < 0)
        return 1.0;
    vec3 fromLight = worldPos - light.Position;
    float distanceToLight = length(fromLight);
    vec3 direction = fromLight / max(distanceToLight, 1e-4);
    float disk = 0.003 + 0.02 * distanceToLight / light.Radius;
    const vec3 offsets[8] = vec3[8](
        vec3(1,1,1), vec3(-1,1,1), vec3(1,-1,1), vec3(-1,-1,1),
        vec3(1,1,-1), vec3(-1,1,-1), vec3(1,-1,-1), vec3(-1,-1,-1));
    float visibility = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        float stored = texture(uPointShadowMaps, vec4(direction + offsets[i] * disk, float(light.ShadowIndex))).r * light.Radius;
        visibility += distanceToLight - 0.035 <= stored ? 1.0 : 0.0;
    }
    return visibility / 8.0;
}

float SamplePointCookie(PointLight light, vec3 worldPos)
{
    if (light.CookieIndex <= 0)
        return 1.0;
    vec3 direction = normalize(worldPos - light.Position);
    vec2 uv = vec2(atan(direction.z, direction.x) / (2.0 * PI) + 0.5, asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);
    return SampleCookie(light.CookieIndex, uv);
}

void ApplyGradientFog(inout vec3 color, vec3 worldPos)
{
    if (!uFogEnabled)
        return;

    float dist = length(worldPos - uCameraPos);
    float distFactor = pow(clamp((dist - uFogStartEnd.x) / max(uFogStartEnd.y - uFogStartEnd.x, 1e-4), 0.0, 1.0), uFogDistanceExponent);
    float heightFactor = pow(clamp((uFogHeightTopBottom.x - worldPos.y) / max(uFogHeightTopBottom.x - uFogHeightTopBottom.y, 1e-4), 0.0, 1.0), uFogHeightExponent);

    float blend = distFactor * heightFactor * uFogOpacity;
    color = mix(color, uFogColor, blend);
}

void main()
{
    vec3 geoNormal = normalize(vNormal);
    vec3 N = geoNormal;

    if (uHasNormalMap)
    {
        vec3 T = normalize(vTangent.xyz - dot(vTangent.xyz, geoNormal) * geoNormal);
        vec3 B = cross(geoNormal, T) * vTangent.w;
        vec3 texNormal = texture(uNormalMap, vUV).rgb * 2.0 - 1.0;
        N = normalize(mat3(T, B, geoNormal) * texNormal);
    }

    vec3 V = normalize(uCameraPos - vWorldPos);

    vec4 baseColor = vec4(uAlbedo, uBaseColorAlpha);
    if (uHasAlbedoMap)
        baseColor *= texture(uAlbedoMap, vUV);
    if (uAlphaMasked && baseColor.a < uAlphaCutoff)
        discard;
    vec3 albedo = baseColor.rgb;

    float metallic = uMetallic;
    float roughness = uRoughness;
    float ao = uAO;
    if (uHasMraoMap)
    {
        vec3 mrao = texture(uMraoMap, vUV).rgb;
        metallic *= mrao.r;
        roughness *= mrao.g;
        ao *= mrao.b;
    }
    if (uHasMetallicRoughnessMap)
    {
        vec3 mr = texture(uMetallicRoughnessMap, vUV).rgb;
        roughness *= mr.g;
        metallic *= mr.b;
    }
    if (uHasOcclusionMap)
        ao *= texture(uOcclusionMap, vUV).r;

    vec3 F0 = mix(vec3(uSpecularF0), albedo, metallic);
    roughness = clamp(roughness, 0.045, 1.0);

    vec3 L0 = vec3(0.0);
    int activeCascade = 0;

    // Sun (directional)
    {
        vec3 L = normalize(-uSunDirection);
        float NoL = max(dot(N, L), 0.0);
        float shadow = uSunCastsShadows && NoL > 0.0 ? SampleCascadedShadow(vWorldPos, NoL, activeCascade) : 1.0;
        L0 += EvaluateLight(N, V, L, uSunColor, albedo, F0, roughness, metallic) * shadow;
    }

    // Point lights
    for (int i = 0; i < uPointLightCount; ++i)
    {
        vec3 toLight = uPointLights[i].Position - vWorldPos;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 1e-4);

        // UE4-style inverse-square falloff with a smooth radius window
        float distRatio = clamp(dist / uPointLights[i].Radius, 0.0, 1.0);
        float window = 1.0 - distRatio * distRatio * distRatio * distRatio;
        float attenuation = (window * window) / (dist * dist + 1.0);

        float shadow = SamplePointShadow(uPointLights[i], vWorldPos);
        float cookie = SamplePointCookie(uPointLights[i], vWorldPos);
        L0 += EvaluateLight(N, V, L, uPointLights[i].Color, albedo, F0, roughness, metallic) * attenuation * shadow * cookie;
    }

    // Spot lights (Source 2 spot: smooth inner->outer cone falloff)
    for (int i = 0; i < uSpotLightCount; ++i)
    {
        vec3 toLight = uSpotLights[i].Position - vWorldPos;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 1e-4);

        float cosAngle = dot(-L, uSpotLights[i].Direction);
        float cone = smoothstep(uSpotLights[i].CosOuter, uSpotLights[i].CosInner, cosAngle);
        if (cone <= 0.0)
            continue;

        float distRatio = clamp(dist / uSpotLights[i].Range, 0.0, 1.0);
        float window = 1.0 - distRatio * distRatio * distRatio * distRatio;
        float attenuation = (window * window) / (dist * dist + 1.0);

        float NoL = max(dot(N, L), 0.0);
        float shadow = NoL > 0.0 ? SampleProjectedShadow(uSpotLights[i].Matrix, uSpotLights[i].ShadowRect, vWorldPos, NoL) : 1.0;
        vec2 projectedUv;
        float projectedDepth;
        float cookie = ProjectLocalLight(uSpotLights[i].Matrix, vWorldPos, projectedUv, projectedDepth)
            ? SampleCookie(uSpotLights[i].CookieIndex, projectedUv) : 0.0;

        L0 += EvaluateLight(N, V, L, uSpotLights[i].Color, albedo, F0, roughness, metallic) * attenuation * cone * shadow * cookie;
    }

    // Source 2 barn/rect-inspired finite rectangular lights.
    for (int i = 0; i < uAreaLightCount; ++i)
    {
        AreaLight light = uAreaLights[i];
        vec3 fromCenter = vWorldPos - light.Position;
        vec3 closest = light.Position
            + light.Right * clamp(dot(fromCenter, light.Right), -light.HalfSize.x, light.HalfSize.x)
            + light.Up * clamp(dot(fromCenter, light.Up), -light.HalfSize.y, light.HalfSize.y);
        vec3 toLight = closest - vWorldPos;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 1e-4);
        float facing = max(dot(-L, light.Direction), 0.0);
        float distRatio = clamp(dist / light.Range, 0.0, 1.0);
        float window = 1.0 - pow(distRatio, 4.0);
        float attenuation = window * window / (dist * dist + 1.0);

        vec2 projectedUv;
        float projectedDepth;
        if (!ProjectLocalLight(light.Matrix, vWorldPos, projectedUv, projectedDepth))
            continue;
        vec2 centered = abs(projectedUv - 0.5);
        vec2 edge = vec2(1.0) - smoothstep(vec2(0.5) - light.Softness * 0.5, vec2(0.5), centered);
        float barn = edge.x * edge.y;
        float cookie = SampleCookie(light.CookieIndex, projectedUv);
        float NoL = max(dot(N, L), 0.0);
        float shadow = NoL > 0.0 ? SampleProjectedShadow(light.Matrix, light.ShadowRect, vWorldPos, NoL) : 1.0;
        float areaRoughness = max(roughness, light.MinRoughness);
        L0 += EvaluateLight(N, V, L, light.Color, albedo, F0, areaRoughness, metallic)
            * attenuation * facing * barn * cookie * shadow;
    }

    // Image-based ambient (split-sum, same shape as VRF EnvBRDF)
    float NoV = max(dot(N, V), 1e-4);
    vec3 F_ambient = F_SchlickRoughness(NoV, F0, roughness);
    vec3 kd = (vec3(1.0) - F_ambient) * (1.0 - metallic);

    vec3 irradiance = texture(uIrradianceMap, N).rgb;
    vec3 diffuseIBL = kd * irradiance * albedo;

    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(uPrefilterMap, R, roughness * (uPrefilterMips - 1.0)).rgb;
    vec2 lut = texture(uBrdfLut, vec2(NoV, roughness)).rg;
    vec3 specularIBL = prefiltered * (F0 * lut.x + lut.y);

    vec3 ambient = (diffuseIBL + specularIBL) * ao;

    vec3 emissive = uEmissive;
    if (uHasEmissiveMap)
        emissive *= texture(uEmissiveMap, vUV).rgb;
    vec3 color = L0 + ambient + emissive;
    if (uDebugCascades)
    {
        const vec3 cascadeColors[4] = vec3[4](
            vec3(1.0, 0.25, 0.25), vec3(0.25, 1.0, 0.25),
            vec3(0.25, 0.45, 1.0), vec3(1.0, 0.85, 0.2));
        color = mix(color, color * cascadeColors[activeCascade], 0.55);
    }
    ApplyGradientFog(color, vWorldPos);

    FragColor = vec4(color, 1.0);
    vec2 currentUv = vCurrentClip.xy / max(vCurrentClip.w, 1e-6) * 0.5 + 0.5;
    vec2 previousUv = vPreviousClip.xy / max(vPreviousClip.w, 1e-6) * 0.5 + 0.5;
    OutVelocity = currentUv - previousUv;
}
