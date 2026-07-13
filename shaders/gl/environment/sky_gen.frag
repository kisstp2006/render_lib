#version 460 core
#include "../../common/procedural_sky.glsl"

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
    vec3 color = EngineEvaluateProceduralSky(dir, toSun, parameters);
    color += EngineEvaluateBakedSun(
        dir, toSun, uSunColor, uSunIntensity, uSunAngularRadius);

    FragColor = vec4(color, 1.0);
}
