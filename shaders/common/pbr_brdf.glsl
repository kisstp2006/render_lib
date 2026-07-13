#ifndef ENGINE_COMMON_PBR_BRDF_GLSL
#define ENGINE_COMMON_PBR_BRDF_GLSL

const float PI = 3.14159265359;

float D_GGX(float nDotH, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = (nDotH * alphaSquared - nDotH) * nDotH + 1.0;
    return alphaSquared / max(PI * denominator * denominator, 1.0e-7);
}

float G_SchlickSmithGGX(float nDotL, float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    float visibilityL = nDotL * (1.0 - k) + k;
    float visibilityV = nDotV * (1.0 - k) + k;
    return 1.0 / max(4.0 * visibilityL * visibilityV, 1.0e-7);
}

vec3 F_Schlick(float cosine, vec3 f0)
{
    return f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 F_SchlickRoughness(float cosine, vec3 f0, float roughness)
{
    return f0 + (max(vec3(1.0 - roughness), f0) - f0)
        * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 EvaluateLight(vec3 normal, vec3 viewDirection, vec3 lightDirection,
                   vec3 radiance, vec3 albedo, vec3 f0, float roughness, float metallic)
{
    vec3 halfway = normalize(viewDirection + lightDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotV = max(dot(normal, viewDirection), 1.0e-4);
    float nDotH = max(dot(normal, halfway), 0.0);
    float vDotH = max(dot(viewDirection, halfway), 0.0);

    float distribution = D_GGX(nDotH, roughness);
    float visibility = G_SchlickSmithGGX(nDotL, nDotV, roughness);
    vec3 fresnel = F_Schlick(vDotH, f0);
    vec3 specular = distribution * visibility * fresnel;
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo / PI;
    return (diffuse + specular) * radiance * nDotL;
}

#endif
