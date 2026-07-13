#version 460
#include "../../common/procedural_sky.glsl"

layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

#include "common/frame_uniforms.glsl"
layout(set = 0, binding = 8) uniform samplerCube environmentMap;

const float PI = 3.14159265358979323846;

float StarHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float ValueNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 fraction = fract(p);
    fraction = fraction * fraction * (3.0 - 2.0 * fraction);
    float a = StarHash(cell);
    float b = StarHash(cell + vec2(1.0, 0.0));
    float c = StarHash(cell + vec2(0.0, 1.0));
    float d = StarHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, fraction.x), mix(c, d, fraction.x), fraction.y);
}

vec3 EvaluateProceduralSky(vec3 direction, vec3 toSun)
{
    EngineProceduralSkyParameters parameters;
    parameters.ZenithColor = frame.SkyZenithIntensity.rgb;
    parameters.HorizonColor = frame.SkyHorizonPostEnabled.rgb;
    parameters.GroundColor = frame.SkyGroundExposure.rgb;
    parameters.NightZenithColor = frame.NightZenithIntensity.rgb;
    parameters.NightHorizonColor = frame.NightHorizonGlow.rgb;
    parameters.SunColor = frame.SunColor.rgb;
    parameters.SkyIntensity = frame.SkyZenithIntensity.w;
    parameters.NightSkyIntensity = frame.NightZenithIntensity.w;
    parameters.NightHorizonGlow = frame.NightHorizonGlow.w;
    parameters.EnableDayNightCycle = frame.SkyFeatureFlags.x > 0.5;
    return EngineEvaluateProceduralSky(direction, toSun, parameters);
}

void main()
{
    vec4 viewRay = inverse(frame.Projection) * vec4(ndc, 1.0, 1.0);
    vec3 viewDirection = normalize(viewRay.xyz / max(abs(viewRay.w), 0.0001));
    vec3 direction = normalize(mat3(inverse(frame.View)) * viewDirection);
    vec3 toSun = normalize(-frame.SunDirectionIntensity.xyz);
    vec3 color = frame.SkySunParameters.w > 0.5
        ? EvaluateProceduralSky(direction, toSun)
        : textureLod(environmentMap, direction, 0.0).rgb;

    if (frame.SkySunParameters.w > 0.5 && frame.SkySunParameters.x > 0.0)
    {
        float angle = acos(clamp(dot(direction, toSun), -1.0, 1.0));
        float edgeWidth = max(fwidth(angle) * 1.5, 0.00005);
        float disk = 1.0 - smoothstep(frame.SkySunParameters.y - edgeWidth,
                                     frame.SkySunParameters.y + edgeWidth, angle);
        float halo = exp(-angle / max(frame.SkySunParameters.y * 7.0, 0.0001));
        float aboveHorizon = smoothstep(-0.02, 0.03, toSun.y)
                           * smoothstep(-0.02, 0.01, direction.y);
        color += frame.SunColor.rgb * (disk * frame.SkySunParameters.x
               + halo * sqrt(frame.SkySunParameters.x) * 0.18) * aboveHorizon;
    }

    if (frame.SkyFeatureFlags.x > 0.5)
    {
        float sunElevation = degrees(asin(clamp(toSun.y, -1.0, 1.0)));
        float nightAmount = 1.0 - smoothstep(-18.0, -6.0, sunElevation);
        float animationTime = frame.SkyFeatureFlags.w > 0.5 ? frame.StarAnimation.w : 0.0;
        float skyRotation = radians(frame.NightRotationFlags.x
                          + animationTime * frame.NightRotationFlags.y);
        mat2 rotation = mat2(cos(skyRotation), -sin(skyRotation),
                             sin(skyRotation),  cos(skyRotation));
        vec2 rotatedXZ = rotation * direction.xz;
        vec3 nightDirection = normalize(vec3(rotatedXZ.x, direction.y, rotatedXZ.y));
        vec2 starUv = vec2(atan(nightDirection.z, nightDirection.x) / (2.0 * PI) + 0.5,
                           asin(clamp(nightDirection.y, -1.0, 1.0)) / PI + 0.5);
        vec2 starGrid = starUv * vec2(720.0, 360.0);
        vec2 starCell = floor(starGrid);
        vec2 starLocal = fract(starGrid) - 0.5;
        float seed = StarHash(starCell);
        float radius = mix(0.045, 0.16, StarHash(starCell + 19.7))
                     * clamp(frame.StarCoolSize.w, 0.25, 4.0);
        float antialias = max(length(fwidth(starGrid)) * 0.22, 0.012);
        float core = 1.0 - smoothstep(radius, radius + antialias, length(starLocal));
        float rareBrightness = pow(StarHash(starCell + 91.3), 8.0);
        float horizontalRay = exp(-abs(starLocal.x) * 34.0) * exp(-abs(starLocal.y) * 5.0);
        float verticalRay = exp(-abs(starLocal.y) * 34.0) * exp(-abs(starLocal.x) * 5.0);
        float star = max(core, (horizontalRay + verticalRay) * rareBrightness * 0.32)
                   * step(1.0 - clamp(frame.StarWarmDensity.w, 0.0, 0.05), seed);
        vec3 starColor = mix(frame.StarWarmDensity.rgb, frame.StarCoolSize.rgb,
                             StarHash(starCell + 47.2));
        float horizonFade = smoothstep(-0.03, 0.14, direction.y);
        float twinklePhase = StarHash(starCell + 73.1) * 2.0 * PI;
        float twinkleSpeed = mix(0.7, 2.4, StarHash(starCell + 12.4));
        float twinkle = mix(1.0, 0.72 + 0.28 * sin(animationTime * twinkleSpeed
                          * max(frame.StarAnimation.z, 0.0) + twinklePhase),
                          clamp(frame.StarAnimation.y, 0.0, 1.0));
        if (frame.SkyFeatureFlags.y > 0.5)
            color += starColor * star * frame.StarAnimation.x * twinkle * nightAmount * horizonFade;

        vec3 galaxyNormal = normalize(vec3(0.24, 0.96, 0.12));
        vec3 galaxyRight = normalize(cross(vec3(0.0, 1.0, 0.0), galaxyNormal));
        vec3 galaxyUp = cross(galaxyNormal, galaxyRight);
        float distanceToBand = abs(dot(nightDirection, galaxyNormal));
        float band = 1.0 - smoothstep(0.05, 0.34, distanceToBand);
        vec2 galaxyUv = vec2(dot(nightDirection, galaxyRight), dot(nightDirection, galaxyUp));
        float dust = ValueNoise(galaxyUv * 8.0) * 0.65 + ValueNoise(galaxyUv * 23.0) * 0.35;
        dust = smoothstep(0.22, 0.88, dust);
        if (frame.SkyFeatureFlags.z > 0.5)
            color += frame.MilkyWayColorIntensity.rgb * band * (0.20 + dust * 0.80)
                   * frame.MilkyWayColorIntensity.w * nightAmount * smoothstep(0.0, 0.18, direction.y);
    }

    color *= frame.SkySunParameters.z;

    vec4 previousClip = frame.PreviousViewProjection * vec4(direction, 0.0);
    vec2 previousUv = previousClip.xy / max(previousClip.w, 1e-6) * 0.5 + 0.5;
    outVelocity = ndc * 0.5 + 0.5 - previousUv;
    outColor = vec4(color, 1.0);
}
