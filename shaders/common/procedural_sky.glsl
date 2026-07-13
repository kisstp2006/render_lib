struct EngineProceduralSkyParameters
{
    vec3 ZenithColor;
    vec3 HorizonColor;
    vec3 GroundColor;
    vec3 NightZenithColor;
    vec3 NightHorizonColor;
    vec3 SunColor;
    float SkyIntensity;
    float NightSkyIntensity;
    float NightHorizonGlow;
    bool EnableDayNightCycle;
};

vec3 EngineEvaluateProceduralSky(vec3 direction, vec3 toSun,
                                 EngineProceduralSkyParameters parameters)
{
    float h = direction.y;
    vec3 daySky = mix(parameters.HorizonColor, parameters.ZenithColor,
                      pow(clamp(h, 0.0, 1.0), 0.55));
    vec3 dayGround = mix(parameters.HorizonColor * 0.85, parameters.GroundColor,
                         pow(clamp(-h, 0.0, 1.0), 0.4));
    vec3 nightSky = mix(parameters.NightHorizonColor * parameters.NightHorizonGlow,
                        parameters.NightZenithColor, pow(clamp(h, 0.0, 1.0), 0.45))
                  * parameters.NightSkyIntensity;
    vec3 nightGround = parameters.NightZenithColor
                     * (0.12 * parameters.NightSkyIntensity);

    float sunElevation = degrees(asin(clamp(toSun.y, -1.0, 1.0)));
    float dayAmount = parameters.EnableDayNightCycle
        ? smoothstep(-6.0, 6.0, sunElevation) : 1.0;
    float nightAmount = parameters.EnableDayNightCycle
        ? 1.0 - smoothstep(-18.0, -6.0, sunElevation) : 0.0;
    float twilightAmount = parameters.EnableDayNightCycle
        ? smoothstep(-18.0, -6.0, sunElevation)
          * (1.0 - smoothstep(2.0, 12.0, sunElevation)) : 0.0;

    vec3 daylightBase = h >= 0.0 ? daySky : dayGround;
    vec3 nightBase = h >= 0.0 ? nightSky : nightGround;
    float twilightWeight = max(0.0, 1.0 - dayAmount - nightAmount);
    vec3 twilightBase = mix(nightBase, daylightBase,
                            smoothstep(-18.0, 0.0, sunElevation));
    vec3 color = (daylightBase * dayAmount + twilightBase * twilightWeight
               + nightBase * nightAmount) * parameters.SkyIntensity;

    vec3 horizonDirection = normalize(vec3(direction.x, 0.0, direction.z)
                                    + vec3(0.0, 0.0, 1e-5));
    vec3 horizonSun = normalize(vec3(toSun.x, 0.0, toSun.z)
                              + vec3(0.0, 0.0, 1e-5));
    float towardSun = pow(max(dot(horizonDirection, horizonSun), 0.0), 3.0);
    float horizonBand = exp(-abs(h) * 7.0);
    vec3 duskColor = mix(vec3(0.18, 0.025, 0.22),
                         vec3(1.55, 0.20, 0.025), towardSun);
    color += duskColor * horizonBand * twilightAmount
           * (0.32 + 0.68 * towardSun) * parameters.SkyIntensity;
    color += parameters.SunColor * pow(max(dot(direction, toSun), 0.0), 12.0)
           * twilightAmount * 0.10;
    return color;
}

vec3 EngineEvaluateBakedSun(vec3 direction, vec3 toSun, vec3 sunColor,
                            float sunIntensity, float angularRadius)
{
    float cosine = dot(direction, toSun);
    float disk = smoothstep(cos(angularRadius), cos(angularRadius * 0.7), cosine);
    float sunVisibility = smoothstep(-0.12, 0.03, toSun.y);
    float aboveGround = smoothstep(-0.03, 0.0, direction.y);
    vec3 glow = sunColor * (pow(clamp(cosine, 0.0, 1.0), 180.0) * 0.5
                          + pow(clamp(cosine, 0.0, 1.0), 8.0) * 0.06);
    return (sunColor * sunIntensity * disk * aboveGround + glow) * sunVisibility;
}
