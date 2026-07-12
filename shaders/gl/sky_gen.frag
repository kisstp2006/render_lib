#version 460 core

// Procedural HDR sky rendered into one cubemap face. Deliberately simple
// (gradient + hot sun disk) but HDR: the sun disk is far above 1.0 so the
// IBL prefilter and bloom get real energy to work with.

in vec2 vNdc;
out vec4 FragColor;

uniform mat3 uFaceBasis; // columns: right, up, forward for this cube face

uniform vec3 uSunDirection; // FROM sun TOWARD scene
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform float uSunAngularRadius; // radians

uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform vec3 uGroundColor;
uniform vec3 uNightZenithColor;
uniform vec3 uNightHorizonColor;
uniform float uNightSkyIntensity;
uniform float uNightHorizonGlow;
uniform float uSkyIntensity;
uniform bool uEnableDayNightCycle;

void main()
{
    vec3 dir = normalize(uFaceBasis * vec3(vNdc, 1.0));
    vec3 toSun = normalize(-uSunDirection);

    float h = dir.y;
    vec3 daySky = mix(uHorizonColor, uZenithColor, pow(clamp(h, 0.0, 1.0), 0.55));
    vec3 dayGround = mix(uHorizonColor * 0.85, uGroundColor, pow(clamp(-h, 0.0, 1.0), 0.4));

    float sunElevation = degrees(asin(clamp(toSun.y, -1.0, 1.0)));
    float dayAmount = uEnableDayNightCycle ? smoothstep(-6.0, 6.0, sunElevation) : 1.0;
    float nightAmount = uEnableDayNightCycle ? 1.0 - smoothstep(-18.0, -6.0, sunElevation) : 0.0;
    float twilightAmount = uEnableDayNightCycle
        ? smoothstep(-18.0, -6.0, sunElevation) * (1.0 - smoothstep(2.0, 12.0, sunElevation)) : 0.0;

    vec3 nightSky = mix(uNightHorizonColor * uNightHorizonGlow, uNightZenithColor,
                        pow(clamp(h, 0.0, 1.0), 0.45)) * uNightSkyIntensity;
    vec3 nightGround = uNightZenithColor * (0.12 * uNightSkyIntensity);
    vec3 daylightBase = h >= 0.0 ? daySky : dayGround;
    vec3 nightBase = h >= 0.0 ? nightSky : nightGround;
    float twilightWeight = max(0.0, 1.0 - dayAmount - nightAmount);
    vec3 twilightBase = mix(nightBase, daylightBase, smoothstep(-18.0, 0.0, sunElevation));
    vec3 color = (daylightBase * dayAmount + twilightBase * twilightWeight + nightBase * nightAmount)
               * uSkyIntensity;

    // Warm scattering remains concentrated around the sunset horizon and in
    // the sun's azimuth through civil and nautical twilight.
    vec3 horizonDir = normalize(vec3(dir.x, 0.0, dir.z) + vec3(0.0, 0.0, 1e-5));
    vec3 horizonSun = normalize(vec3(toSun.x, 0.0, toSun.z) + vec3(0.0, 0.0, 1e-5));
    float towardSun = pow(max(dot(horizonDir, horizonSun), 0.0), 3.0);
    float horizonBand = exp(-abs(h) * 7.0);
    vec3 duskColor = mix(vec3(0.18, 0.025, 0.22), vec3(1.55, 0.20, 0.025), towardSun);
    color += duskColor * horizonBand * twilightAmount * (0.32 + 0.68 * towardSun) * uSkyIntensity;
    color += uSunColor * pow(max(dot(dir, toSun), 0.0), 12.0) * twilightAmount * 0.10;

    // Sun disk + glow, faded out as the sun dips below the horizon
    float cosAngle = dot(dir, toSun);
    float cosRadius = cos(uSunAngularRadius);
    float disk = smoothstep(cosRadius, cos(uSunAngularRadius * 0.7), cosAngle);
    float sunVis = smoothstep(-0.12, 0.03, toSun.y);
    float aboveGround = smoothstep(-0.03, 0.0, h);

    vec3 glow = uSunColor * (pow(clamp(cosAngle, 0.0, 1.0), 180.0) * 0.5
                           + pow(clamp(cosAngle, 0.0, 1.0), 8.0) * 0.06);
    color += (uSunColor * uSunIntensity * disk * aboveGround + glow) * sunVis;

    FragColor = vec4(color, 1.0);
}
