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
in vec4 vLightSpacePos;

out vec4 FragColor;

uniform vec3 uCameraPos;
uniform vec3 uSunDirection; // points FROM the sun TOWARD the scene
uniform vec3 uSunColor;
uniform bool uSunCastsShadows;

struct PointLight
{
    vec3 Position;
    vec3 Color;
    float Radius;
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
};

uniform int uSpotLightCount;
uniform SpotLight uSpotLights[4];
uniform int uSpotShadowIndex; // which spot samples the spot shadow map, -1 = none
uniform mat4 uSpotShadowMatrix;
uniform sampler2D uSpotShadowMap; // unit 7

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
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform float uAO;
uniform float uSpecularF0;

// Texture maps (MRAO: R=metal, G=roughness, B=AO - Source 2 convention)
uniform bool uHasAlbedoMap;
uniform bool uHasNormalMap;
uniform bool uHasMraoMap;
uniform sampler2D uAlbedoMap; // unit 1
uniform sampler2D uNormalMap; // unit 2
uniform sampler2D uMraoMap;   // unit 3

uniform sampler2D uShadowMap;        // unit 0
uniform samplerCube uIrradianceMap;  // unit 4
uniform samplerCube uPrefilterMap;   // unit 5
uniform sampler2D uBrdfLut;          // unit 6
uniform float uPrefilterMips;

const float PI = 3.14159265359;

float D_GGX(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float G_SchlickSmithGGX(float NoL, float NoV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float visL = NoL * (1.0 - k) + k;
    float visV = NoV * (1.0 - k) + k;
    return 1.0 / max(4.0 * visL * visV, 1e-7);
}

vec3 F_Schlick(float cosTheta, vec3 F0)
{
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 F_SchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EvaluateLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0, float roughness, float metallic)
{
    vec3 H = normalize(V + L);
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 1e-4);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    float D = D_GGX(NoH, roughness);
    float Vis = G_SchlickSmithGGX(NoL, NoV, roughness);
    vec3 F = F_Schlick(VoH, F0);

    vec3 specular = D * Vis * F;
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kd * albedo / PI;

    return (diffuse + specular) * radiance * NoL;
}

float SampleShadow(vec4 lightSpacePos, float NoL)
{
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    float bias = max(0.0025 * (1.0 - NoL), 0.0005);
    float shadow = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float closestDepth = texture(uShadowMap, proj.xy + vec2(x, y) * texel).r;
            shadow += (proj.z - bias) > closestDepth ? 0.0 : 1.0;
        }
    }

    return shadow / 9.0;
}

float SampleSpotShadow(vec3 worldPos, float NoL)
{
    vec4 lightSpacePos = uSpotShadowMatrix * vec4(worldPos, 1.0);
    if (lightSpacePos.w <= 0.0)
        return 1.0;

    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    float bias = max(0.002 * (1.0 - NoL), 0.0004);
    float shadow = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uSpotShadowMap, 0));

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float closestDepth = texture(uSpotShadowMap, proj.xy + vec2(x, y) * texel).r;
            shadow += (proj.z - bias) > closestDepth ? 0.0 : 1.0;
        }
    }

    return shadow / 9.0;
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

    vec3 albedo = uAlbedo;
    if (uHasAlbedoMap)
        albedo *= texture(uAlbedoMap, vUV).rgb;

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

    vec3 F0 = mix(vec3(uSpecularF0), albedo, metallic);
    roughness = clamp(roughness, 0.045, 1.0);

    vec3 L0 = vec3(0.0);

    // Sun (directional)
    {
        vec3 L = normalize(-uSunDirection);
        float NoL = max(dot(N, L), 0.0);
        float shadow = uSunCastsShadows && NoL > 0.0 ? SampleShadow(vLightSpacePos, NoL) : 1.0;
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

        L0 += EvaluateLight(N, V, L, uPointLights[i].Color, albedo, F0, roughness, metallic) * attenuation;
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

        float shadow = 1.0;
        if (i == uSpotShadowIndex)
        {
            float NoL = max(dot(N, L), 0.0);
            shadow = NoL > 0.0 ? SampleSpotShadow(vWorldPos, NoL) : 1.0;
        }

        L0 += EvaluateLight(N, V, L, uSpotLights[i].Color, albedo, F0, roughness, metallic) * attenuation * cone * shadow;
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

    vec3 color = L0 + ambient + uEmissive;
    ApplyGradientFog(color, vWorldPos);

    FragColor = vec4(color, 1.0);
}
