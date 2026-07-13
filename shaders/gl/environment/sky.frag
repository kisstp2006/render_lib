#version 460 core
#include "../../common/procedural_sky.glsl"
#include "../../common/procedural_stars.glsl"

in vec2 vNdc;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec2 OutVelocity;

uniform samplerCube uEnvMap;
uniform mat4 uInvProj;
uniform mat4 uInvView;
uniform float uBackgroundMultiplier;
uniform bool uUseProceduralSky;
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform vec3 uGroundColor;
uniform vec3 uNightZenithColor;
uniform vec3 uNightHorizonColor;
uniform vec3 uMilkyWayColor;
uniform vec3 uStarWarmColor;
uniform vec3 uStarCoolColor;
uniform float uSkyIntensity;
uniform float uNightSkyIntensity;
uniform float uNightHorizonGlow;
uniform bool uDrawProceduralSun;
uniform vec3 uSunDirection; // FROM the sun TOWARD the scene
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform float uSunAngularRadius;
uniform bool uEnableDayNightCycle;
uniform float uStarIntensity;
uniform float uStarDensity;
uniform float uStarSize;
uniform float uStarTwinkle;
uniform float uStarTwinkleSpeed;
uniform float uMilkyWayIntensity;
uniform float uNightSkyRotationDegrees;
uniform float uNightSkyRotationSpeed;
uniform bool uStarsEnabled;
uniform bool uMilkyWayEnabled;
uniform bool uAnimateNightSky;
uniform float uTime;
uniform mat4 uPreviousViewProjection;

float ValueNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 fraction = fract(p);
    fraction = fraction * fraction * (3.0 - 2.0 * fraction);
    float a = EngineStarHash(cell);
    float b = EngineStarHash(cell + vec2(1.0, 0.0));
    float c = EngineStarHash(cell + vec2(0.0, 1.0));
    float d = EngineStarHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, fraction.x), mix(c, d, fraction.x), fraction.y);
}

vec3 EvaluateProceduralSky(vec3 direction, vec3 toSun)
{
    EngineProceduralSkyParameters parameters;
    parameters.ZenithColor = uZenithColor;
    parameters.HorizonColor = uHorizonColor;
    parameters.GroundColor = uGroundColor;
    parameters.NightZenithColor = uNightZenithColor;
    parameters.NightHorizonColor = uNightHorizonColor;
    parameters.SunColor = uSunColor;
    parameters.SkyIntensity = uSkyIntensity;
    parameters.NightSkyIntensity = uNightSkyIntensity;
    parameters.NightHorizonGlow = uNightHorizonGlow;
    parameters.EnableDayNightCycle = uEnableDayNightCycle;
    return EngineEvaluateProceduralSky(direction, toSun, parameters);
}

void main()
{
    vec4 viewRay = uInvProj * vec4(vNdc, 1.0, 1.0);
    vec3 dirView = normalize(viewRay.xyz / viewRay.w);
    vec3 dirWorld = normalize(mat3(uInvView) * dirView);

    vec3 toSun = normalize(-uSunDirection);
    vec3 color = uUseProceduralSky
        ? EvaluateProceduralSky(dirWorld, toSun)
        : textureLod(uEnvMap, dirWorld, 0.0).rgb;

    // Lumix-style coupling: the visible sun and directional lighting share
    // one world-space direction. Render the core at screen resolution so it
    // stays round and stable instead of being limited by the IBL cubemap.
    if (uDrawProceduralSun)
    {
        float angle = acos(clamp(dot(dirWorld, toSun), -1.0, 1.0));
        float edgeWidth = max(fwidth(angle) * 1.5, 0.00005);
        float disk = 1.0 - smoothstep(uSunAngularRadius - edgeWidth,
                                     uSunAngularRadius + edgeWidth, angle);
        float halo = exp(-angle / max(uSunAngularRadius * 7.0, 0.0001));
        float aboveHorizon = smoothstep(-0.02, 0.03, toSun.y)
                           * smoothstep(-0.02, 0.01, dirWorld.y);
        color += uSunColor * (disk * uSunIntensity + halo * sqrt(max(uSunIntensity, 0.0)) * 0.18)
               * aboveHorizon;
    }

    if (uEnableDayNightCycle)
    {
        float sunElevation = degrees(asin(clamp(toSun.y, -1.0, 1.0)));
        float nightAmount = 1.0 - smoothstep(-18.0, -6.0, sunElevation);

        float animationTime = uAnimateNightSky ? uTime : 0.0;
        float skyRotation = radians(uNightSkyRotationDegrees + animationTime * uNightSkyRotationSpeed);
        mat2 skyRotationMatrix = mat2(cos(skyRotation), -sin(skyRotation),
                                      sin(skyRotation),  cos(skyRotation));
        vec2 rotatedXZ = skyRotationMatrix * dirWorld.xz;
        vec3 nightDirection = normalize(vec3(rotatedXZ.x, dirWorld.y, rotatedXZ.y));

        // Stable world-space cubemap cells avoid the latitude/longitude pole
        // singularity while remaining deterministic and texture-free.
        EngineProceduralStar star = EngineEvaluateProceduralStar(
            nightDirection, uStarDensity, uStarSize);
        vec3 starColor = mix(uStarWarmColor, uStarCoolColor, star.Temperature);
        float horizonFade = smoothstep(-0.03, 0.14, dirWorld.y);
        float twinkle = mix(1.0, 0.72 + 0.28 * sin(animationTime * star.TwinkleSpeed
                                                   * max(uStarTwinkleSpeed, 0.0) + star.TwinklePhase),
                            clamp(uStarTwinkle, 0.0, 1.0));
        if (uStarsEnabled)
            color += starColor * star.Shape * uStarIntensity * twinkle * nightAmount * horizonFade;

        // A subtle procedural galactic band makes the night sky less empty
        // without requiring a texture or affecting IBL/reflection probes.
        vec3 galaxyNormal = normalize(vec3(0.24, 0.96, 0.12));
        vec3 galaxyRight = normalize(cross(vec3(0.0, 1.0, 0.0), galaxyNormal));
        vec3 galaxyUp = cross(galaxyNormal, galaxyRight);
        float distanceToBand = abs(dot(nightDirection, galaxyNormal));
        float band = 1.0 - smoothstep(0.05, 0.34, distanceToBand);
        vec2 galaxyUv = vec2(dot(nightDirection, galaxyRight), dot(nightDirection, galaxyUp));
        float dust = ValueNoise(galaxyUv * 8.0) * 0.65 + ValueNoise(galaxyUv * 23.0) * 0.35;
        dust = smoothstep(0.22, 0.88, dust);
        if (uMilkyWayEnabled)
            color += uMilkyWayColor * band * (0.20 + dust * 0.80) * uMilkyWayIntensity
                   * nightAmount * smoothstep(0.0, 0.18, dirWorld.y);
    }

    FragColor = vec4(color * uBackgroundMultiplier, 1.0);
    vec4 previousClip = uPreviousViewProjection * vec4(dirWorld, 0.0);
    vec2 previousUv = previousClip.xy / max(previousClip.w, 1e-6) * 0.5 + 0.5;
    OutVelocity = vNdc * 0.5 + 0.5 - previousUv;
}
