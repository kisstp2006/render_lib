#version 460 core
#include "../../common/procedural_sky.glsl"

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

        // Stable world-space equirectangular cells: no texture asset and no
        // camera-relative swimming. Brightness/color vary deterministically.
        vec2 starUv = vec2(atan(nightDirection.z, nightDirection.x) / (2.0 * PI) + 0.5,
                           asin(clamp(nightDirection.y, -1.0, 1.0)) / PI + 0.5);
        vec2 starGrid = starUv * vec2(720.0, 360.0);
        vec2 starCell = floor(starGrid);
        vec2 starLocal = fract(starGrid) - 0.5;
        float seed = StarHash(starCell);
        float radius = mix(0.045, 0.16, StarHash(starCell + 19.7)) * clamp(uStarSize, 0.25, 4.0);
        float antialias = max(length(fwidth(starGrid)) * 0.22, 0.012);
        float core = 1.0 - smoothstep(radius, radius + antialias, length(starLocal));
        float rareBrightness = pow(StarHash(starCell + 91.3), 8.0);
        float horizontalRay = exp(-abs(starLocal.x) * 34.0) * exp(-abs(starLocal.y) * 5.0);
        float verticalRay = exp(-abs(starLocal.y) * 34.0) * exp(-abs(starLocal.x) * 5.0);
        float star = max(core, (horizontalRay + verticalRay) * rareBrightness * 0.32)
                   * step(1.0 - clamp(uStarDensity, 0.0, 0.05), seed);
        float temperature = StarHash(starCell + 47.2);
        vec3 starColor = mix(uStarWarmColor, uStarCoolColor, temperature);
        float horizonFade = smoothstep(-0.03, 0.14, dirWorld.y);
        float twinklePhase = StarHash(starCell + 73.1) * 2.0 * PI;
        float twinkleSpeed = mix(0.7, 2.4, StarHash(starCell + 12.4));
        float twinkle = mix(1.0, 0.72 + 0.28 * sin(animationTime * twinkleSpeed
                                                   * max(uStarTwinkleSpeed, 0.0) + twinklePhase),
                            clamp(uStarTwinkle, 0.0, 1.0));
        if (uStarsEnabled)
            color += starColor * star * uStarIntensity * twinkle * nightAmount * horizonFade;

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
