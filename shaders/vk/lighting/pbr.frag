#version 460

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec4 worldTangent;
layout(location = 3) in vec2 uv;
layout(location = 4) in vec4 currentClip;
layout(location = 5) in vec4 previousClip;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

#include "common/frame_uniforms.glsl"

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilteredMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLut;
layout(set = 0, binding = 4) uniform sampler2D shadowMap0;
layout(set = 0, binding = 5) uniform sampler2D shadowMap1;
layout(set = 0, binding = 6) uniform sampler2D shadowMap2;
layout(set = 0, binding = 7) uniform sampler2D shadowMap3;
layout(set = 0, binding = 9) uniform samplerCubeArray pointShadowMaps;
layout(set = 0, binding = 10) uniform sampler2D localShadowAtlas;
layout(set = 0, binding = 11) uniform sampler2D lightCookieAtlas;

layout(set = 1, binding = 0, std140) uniform MaterialUniforms
{
    vec4 BaseColorFactor;
    vec4 EmissiveMetallic;
    vec4 RoughnessAoAlphaCutoff;
    uvec4 TextureFlags;
} material;

layout(set = 1, binding = 1) uniform sampler2D baseColorMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D occlusionMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

const uint HAS_BASE_COLOR_MAP = 1u << 0u;
const uint HAS_NORMAL_MAP = 1u << 1u;
const uint HAS_METALLIC_ROUGHNESS_MAP = 1u << 2u;
const uint HAS_OCCLUSION_MAP = 1u << 3u;
const uint HAS_EMISSIVE_MAP = 1u << 4u;
const uint USES_ALPHA_MASK = 1u << 5u;
const uint HAS_LEGACY_MRAO_MAP = 1u << 6u;

#include "../../common/pbr_brdf.glsl"

float ReadShadowDepth(int cascade, vec2 uv)
{
    if (cascade == 0) return texture(shadowMap0, uv).r;
    if (cascade == 1) return texture(shadowMap1, uv).r;
    if (cascade == 2) return texture(shadowMap2, uv).r;
    return texture(shadowMap3, uv).r;
}

vec2 ShadowTexelSize(int cascade)
{
    if (cascade == 0) return 1.0 / vec2(textureSize(shadowMap0, 0));
    if (cascade == 1) return 1.0 / vec2(textureSize(shadowMap1, 0));
    if (cascade == 2) return 1.0 / vec2(textureSize(shadowMap2, 0));
    return 1.0 / vec2(textureSize(shadowMap3, 0));
}

float SampleCascadeShadow(int cascade, vec3 position, float nDotL)
{
    vec4 lightPosition = frame.CascadeMatrices[cascade] * vec4(position, 1.0);
    vec3 projected = lightPosition.xyz / lightPosition.w;
    vec2 uv = projected.xy * 0.5 + 0.5;
    if (projected.z < 0.0 || projected.z > 1.0 || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return 1.0;

    float bias = max(0.0025 * (1.0 - nDotL), 0.0005);
    vec2 texel = ShadowTexelSize(cascade);
    float visibility = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            visibility += projected.z - bias <= ReadShadowDepth(cascade, uv + vec2(x, y) * texel) ? 1.0 : 0.0;
    return visibility / 9.0;
}

float SampleSunShadow(vec3 position, float nDotL, out int activeCascade)
{
    float viewDepth = -(frame.View * vec4(position, 1.0)).z;
    activeCascade = 3;
    for (int cascade = 0; cascade < 4; ++cascade)
    {
        if (viewDepth <= frame.CascadeSplits[cascade])
        {
            activeCascade = cascade;
            break;
        }
    }
    if (viewDepth > frame.CascadeSplits.w)
        return 1.0;

    float visibility = SampleCascadeShadow(activeCascade, position, nDotL);
    if (activeCascade < 3)
    {
        float nearDepth = activeCascade == 0 ? 0.0 : frame.CascadeSplits[activeCascade - 1];
        float blendWidth = (frame.CascadeSplits[activeCascade] - nearDepth) * frame.ShadowParameters.x;
        float blendStart = frame.CascadeSplits[activeCascade] - blendWidth;
        if (viewDepth > blendStart)
        {
            float blend = clamp((viewDepth - blendStart) / max(blendWidth, 0.0001), 0.0, 1.0);
            visibility = mix(visibility, SampleCascadeShadow(activeCascade + 1, position, nDotL), blend);
        }
    }
    return visibility;
}

float SampleCookie(int slot, vec2 cookieUv)
{
    if (slot <= 0)
        return 1.0;
    const float scale = 0.25;
    vec2 offset = vec2(slot % 4, slot / 4) * scale;
    return texture(lightCookieAtlas, offset + clamp(cookieUv, 0.0, 1.0) * scale).r;
}

bool ProjectLocalLight(mat4 matrix, vec3 position, out vec2 projectedUv, out float depth)
{
    vec4 clip = matrix * vec4(position, 1.0);
    if (clip.w <= 0.0)
        return false;
    vec3 projected = clip.xyz / clip.w;
    projectedUv = projected.xy * 0.5 + 0.5;
    depth = projected.z;
    return depth >= 0.0 && depth <= 1.0
        && all(greaterThanEqual(projectedUv, vec2(0.0)))
        && all(lessThanEqual(projectedUv, vec2(1.0)));
}

float SampleProjectedShadow(mat4 matrix, vec4 atlasRect, vec3 position, float nDotL)
{
    if (atlasRect.z <= 0.0)
        return 1.0;
    vec2 projectedUv;
    float depth;
    if (!ProjectLocalLight(matrix, position, projectedUv, depth))
        return 1.0;
    float bias = max(0.002 * (1.0 - nDotL), 0.0004);
    vec2 texel = 1.0 / vec2(textureSize(localShadowAtlas, 0));
    vec2 atlasUv = atlasRect.xy + projectedUv * atlasRect.zw;
    vec2 atlasMin = atlasRect.xy + texel * 0.5;
    vec2 atlasMax = atlasRect.xy + atlasRect.zw - texel * 0.5;
    float visibility = 0.0;
    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    {
        vec2 sampleUv = clamp(atlasUv + vec2(x, y) * texel, atlasMin, atlasMax);
        visibility += depth - bias <= texture(localShadowAtlas, sampleUv).r ? 1.0 : 0.0;
    }
    return visibility / 9.0;
}

float SamplePointShadow(uint lightIndex, vec3 position)
{
    int shadowIndex = int(round(frame.PointShadowCookie[lightIndex].x));
    if (shadowIndex < 0)
        return 1.0;
    vec3 fromLight = position - frame.PointPositionRadius[lightIndex].xyz;
    float distanceToLight = length(fromLight);
    vec3 direction = fromLight / max(distanceToLight, 0.0001);
    float radius = frame.PointPositionRadius[lightIndex].w;
    float disk = 0.003 + 0.02 * distanceToLight / radius;
    const vec3 offsets[8] = vec3[8](
        vec3(1,1,1), vec3(-1,1,1), vec3(1,-1,1), vec3(-1,-1,1),
        vec3(1,1,-1), vec3(-1,1,-1), vec3(1,-1,-1), vec3(-1,-1,-1));
    float visibility = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        float stored = texture(pointShadowMaps,
            vec4(direction + offsets[i] * disk, float(shadowIndex))).r * radius;
        visibility += distanceToLight - 0.035 <= stored ? 1.0 : 0.0;
    }
    return visibility / 8.0;
}

float SamplePointCookie(uint lightIndex, vec3 position)
{
    int slot = int(round(frame.PointShadowCookie[lightIndex].y));
    if (slot <= 0)
        return 1.0;
    vec3 direction = normalize(position - frame.PointPositionRadius[lightIndex].xyz);
    vec2 cookieUv = vec2(atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
                         asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);
    return SampleCookie(slot, cookieUv);
}

void main()
{
    vec4 baseColor = material.BaseColorFactor;
    uint flags = material.TextureFlags.x;
    if ((flags & HAS_BASE_COLOR_MAP) != 0u)
        baseColor *= texture(baseColorMap, uv);
    if ((flags & USES_ALPHA_MASK) != 0u && baseColor.a < material.RoughnessAoAlphaCutoff.z)
        discard;

    float metallic = material.EmissiveMetallic.w;
    float roughness = material.RoughnessAoAlphaCutoff.x;
    if ((flags & HAS_METALLIC_ROUGHNESS_MAP) != 0u)
    {
        vec4 mr = texture(metallicRoughnessMap, uv);
        roughness *= mr.g;
        metallic *= mr.b;
    }
    else if ((flags & HAS_LEGACY_MRAO_MAP) != 0u)
    {
        vec4 mrao = texture(metallicRoughnessMap, uv);
        metallic *= mrao.r;
        roughness *= mrao.g;
    }
    roughness = clamp(roughness, 0.045, 1.0);

    vec3 normal = normalize(worldNormal);
    if ((flags & HAS_NORMAL_MAP) != 0u)
    {
        vec3 tangent = normalize(worldTangent.xyz);
        vec3 bitangent = normalize(cross(normal, tangent)) * worldTangent.w;
        vec2 normalXY = texture(normalMap, uv).rg * 2.0 - 1.0;
        vec3 mapped = vec3(normalXY, sqrt(max(1.0 - dot(normalXY, normalXY), 0.0)));
        normal = normalize(mat3(tangent, bitangent, normal) * mapped);
    }

    float ao = material.RoughnessAoAlphaCutoff.y;
    if ((flags & HAS_LEGACY_MRAO_MAP) != 0u)
        ao *= texture(metallicRoughnessMap, uv).b;
    if ((flags & HAS_OCCLUSION_MAP) != 0u)
        ao *= texture(occlusionMap, uv).r;

    vec3 emissive = material.EmissiveMetallic.rgb;
    if ((flags & HAS_EMISSIVE_MAP) != 0u)
        emissive *= texture(emissiveMap, uv).rgb;

    vec3 viewDirection = normalize(frame.CameraPosition.xyz - worldPosition);
    vec3 lightDirection = normalize(-frame.SunDirectionIntensity.xyz);
    float nDotV = max(dot(normal, viewDirection), 0.0);
    float nDotL = max(dot(normal, lightDirection), 0.0);

    vec3 f0 = mix(vec3(material.RoughnessAoAlphaCutoff.w), baseColor.rgb, metallic);
    int activeCascade = 0;
    float sunVisibility = frame.ShadowParameters.y > 0.5 && nDotL > 0.0
        ? SampleSunShadow(worldPosition, nDotL, activeCascade) : 1.0;
    vec3 direct = EvaluateLight(normal, viewDirection, lightDirection,
        frame.SunColor.rgb * frame.SunDirectionIntensity.w,
        baseColor.rgb, f0, roughness, metallic) * sunVisibility;

    for (uint i = 0u; i < min(frame.LightCounts.x, 8u); ++i)
    {
        vec3 toLight = frame.PointPositionRadius[i].xyz - worldPosition;
        float distanceToLight = length(toLight);
        vec3 localDirection = toLight / max(distanceToLight, 0.0001);
        float ratio = clamp(distanceToLight / frame.PointPositionRadius[i].w, 0.0, 1.0);
        float window = 1.0 - ratio * ratio * ratio * ratio;
        float attenuation = window * window / (distanceToLight * distanceToLight + 1.0);
        direct += EvaluateLight(normal, viewDirection, localDirection, frame.PointColor[i].rgb,
            baseColor.rgb, f0, roughness, metallic) * attenuation
            * SamplePointShadow(i, worldPosition) * SamplePointCookie(i, worldPosition);
    }

    for (uint i = 0u; i < min(frame.LightCounts.y, 4u); ++i)
    {
        vec3 toLight = frame.SpotPositionRange[i].xyz - worldPosition;
        float distanceToLight = length(toLight);
        vec3 localDirection = toLight / max(distanceToLight, 0.0001);
        float coneAngle = dot(-localDirection, frame.SpotDirectionCosOuter[i].xyz);
        float cone = smoothstep(frame.SpotDirectionCosOuter[i].w, frame.SpotColorCosInner[i].w, coneAngle);
        float ratio = clamp(distanceToLight / frame.SpotPositionRange[i].w, 0.0, 1.0);
        float window = 1.0 - ratio * ratio * ratio * ratio;
        float attenuation = window * window / (distanceToLight * distanceToLight + 1.0);
        float localNdotL = max(dot(normal, localDirection), 0.0);
        float shadow = localNdotL > 0.0
            ? SampleProjectedShadow(frame.SpotMatrices[i], frame.SpotShadowRects[i],
                                    worldPosition, localNdotL) : 1.0;
        vec2 projectedUv;
        float projectedDepth;
        float cookie = ProjectLocalLight(frame.SpotMatrices[i], worldPosition,
                                         projectedUv, projectedDepth)
            ? SampleCookie(int(round(frame.SpotCookieData[i].x)), projectedUv) : 0.0;
        direct += EvaluateLight(normal, viewDirection, localDirection, frame.SpotColorCosInner[i].rgb,
            baseColor.rgb, f0, roughness, metallic) * attenuation * cone * shadow * cookie;
    }

    for (uint i = 0u; i < min(frame.LightCounts.z, 4u); ++i)
    {
        vec3 fromCenter = worldPosition - frame.AreaPositionRange[i].xyz;
        vec3 closest = frame.AreaPositionRange[i].xyz
            + frame.AreaRightHalfWidth[i].xyz * clamp(dot(fromCenter, frame.AreaRightHalfWidth[i].xyz),
                -frame.AreaRightHalfWidth[i].w, frame.AreaRightHalfWidth[i].w)
            + frame.AreaUpHalfHeight[i].xyz * clamp(dot(fromCenter, frame.AreaUpHalfHeight[i].xyz),
                -frame.AreaUpHalfHeight[i].w, frame.AreaUpHalfHeight[i].w);
        vec3 toLight = closest - worldPosition;
        float distanceToLight = length(toLight);
        vec3 localDirection = toLight / max(distanceToLight, 0.0001);
        float facing = max(dot(-localDirection, frame.AreaDirectionMinRoughness[i].xyz), 0.0);
        float ratio = clamp(distanceToLight / frame.AreaPositionRange[i].w, 0.0, 1.0);
        float window = 1.0 - ratio * ratio * ratio * ratio;
        float attenuation = window * window / (distanceToLight * distanceToLight + 1.0);
        vec2 projectedUv;
        float projectedDepth;
        if (!ProjectLocalLight(frame.AreaMatrices[i], worldPosition, projectedUv, projectedDepth))
            continue;
        vec2 centered = abs(projectedUv - 0.5);
        vec2 softness = frame.AreaSoftness[i].xy;
        vec2 edge = vec2(1.0) - smoothstep(vec2(0.5) - softness * 0.5, vec2(0.5), centered);
        float barn = edge.x * edge.y;
        float cookie = SampleCookie(int(round(frame.AreaCookieData[i].x)), projectedUv);
        float localNdotL = max(dot(normal, localDirection), 0.0);
        float shadow = localNdotL > 0.0
            ? SampleProjectedShadow(frame.AreaMatrices[i], frame.AreaShadowRects[i],
                                    worldPosition, localNdotL) : 1.0;
        direct += EvaluateLight(normal, viewDirection, localDirection, frame.AreaColor[i].rgb,
            baseColor.rgb, f0, max(roughness, frame.AreaDirectionMinRoughness[i].w), metallic)
            * attenuation * facing * barn * cookie * shadow;
    }

    vec3 ambientFresnel = F_SchlickRoughness(nDotV, f0, roughness);
    vec3 reflection = reflect(-viewDirection, normal);
    vec3 diffuseEnvironment = texture(irradianceMap, normal).rgb;
    vec3 diffuseAmbient = (1.0 - ambientFresnel) * (1.0 - metallic)
                        * diffuseEnvironment * baseColor.rgb;
    vec3 specularEnvironment = textureLod(prefilteredMap, reflection, roughness * 7.0).rgb;
    vec2 integratedBrdf = texture(brdfLut, vec2(nDotV, roughness)).rg;
    vec3 specularAmbient = specularEnvironment * (f0 * integratedBrdf.x + integratedBrdf.y);
    vec3 color = (diffuseAmbient + specularAmbient) * ao + direct + emissive;

    if (frame.ShadowParameters.z > 0.5)
    {
        const vec3 cascadeColors[4] = vec3[4](
            vec3(1.0, 0.25, 0.25), vec3(0.25, 1.0, 0.25),
            vec3(0.25, 0.45, 1.0), vec3(1.0, 0.85, 0.2));
        color = mix(color, color * cascadeColors[activeCascade], 0.55);
    }

    if (frame.FogHeightEnabled.z > 0.5)
    {
        float distanceToCamera = length(worldPosition - frame.CameraPosition.xyz);
        float distanceFog = pow(clamp((distanceToCamera - frame.FogStartEndExponents.x)
                           / max(frame.FogStartEndExponents.y - frame.FogStartEndExponents.x, 0.0001), 0.0, 1.0),
                           frame.FogStartEndExponents.z);
        float heightFog = pow(clamp((frame.FogHeightEnabled.x - worldPosition.y)
                         / max(frame.FogHeightEnabled.x - frame.FogHeightEnabled.y, 0.0001), 0.0, 1.0),
                         frame.FogStartEndExponents.w);
        color = mix(color, frame.FogColorOpacity.rgb,
                    distanceFog * heightFog * frame.FogColorOpacity.w);
    }

    vec2 currentUv = currentClip.xy / max(currentClip.w, 1e-6) * 0.5 + 0.5;
    vec2 previousUv = previousClip.xy / max(previousClip.w, 1e-6) * 0.5 + 0.5;
    outVelocity = currentUv - previousUv;
    outColor = vec4(color, baseColor.a);
}
