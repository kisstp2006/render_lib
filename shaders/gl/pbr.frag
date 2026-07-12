#version 460 core

// Cook-Torrance GGX PBR, following the same D/G/F terms Source 2 uses in
// pbr.slang (D_GGX, G_SchlickSmithGGX, F_Schlick) so the specular response
// reads the same way Source 2 materials do. Ambient/indirect lighting here
// is a flat two-tone (sky/ground) approximation rather than real IBL - see
// README roadmap for baked irradiance / prefiltered environment maps.

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLightSpacePos;

out vec4 FragColor;

uniform vec3 uCameraPos;
uniform vec3 uSunDirection; // points FROM the sun TOWARD the scene
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;

struct PointLight
{
    vec3 Position;
    vec3 Color;
    float Radius;
};

uniform int uPointLightCount;
uniform PointLight uPointLights[8];

uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform float uAO;
uniform float uSpecularF0;

uniform sampler2D uShadowMap;

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

// Cheap filmic-ish tonemap (Uncharted2-style) to avoid the washed-out look of
// plain Reinhard, closer to Source 2's default tonemapping response.
vec3 Tonemap(vec3 color)
{
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    color = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
    vec3 white = vec3(11.2);
    vec3 whiteScale = ((white * (A * white + C * B) + D * E) / (white * (A * white + B) + D * F)) - E / F;
    return color / whiteScale;
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    vec3 albedo = uAlbedo;
    vec3 F0 = mix(vec3(uSpecularF0), albedo, uMetallic);
    float roughness = clamp(uRoughness, 0.045, 1.0);

    vec3 L0 = vec3(0.0);

    // Sun (directional)
    {
        vec3 L = normalize(-uSunDirection);
        float NoL = max(dot(N, L), 0.0);
        float shadow = NoL > 0.0 ? SampleShadow(vLightSpacePos, NoL) : 1.0;
        L0 += EvaluateLight(N, V, L, uSunColor, albedo, F0, roughness, uMetallic) * shadow;
    }

    // Point lights
    for (int i = 0; i < uPointLightCount; ++i)
    {
        vec3 toLight = uPointLights[i].Position - vWorldPos;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 1e-4);

        float distRatio = clamp(dist / uPointLights[i].Radius, 0.0, 1.0);
        float attenuation = pow(1.0 - distRatio, 2.0) / (1.0 + dist * dist);

        L0 += EvaluateLight(N, V, L, uPointLights[i].Color, albedo, F0, roughness, uMetallic) * attenuation;
    }

    // Cheap two-tone "sky/ground" ambient in place of a real IBL probe -
    // gives metals a plausible reflection tint instead of going flat black.
    float skyFactor = N.y * 0.5 + 0.5;
    vec3 ambientLight = mix(uAmbientColor * 0.5, uAmbientColor, skyFactor);
    vec3 ambientDiffuse = ambientLight * albedo * (1.0 - uMetallic);
    vec3 ambientSpecular = ambientLight * F0;
    vec3 ambient = (ambientDiffuse + ambientSpecular) * uAO;

    vec3 color = L0 + ambient + uEmissive;

    color = Tonemap(color * 1.2);
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
