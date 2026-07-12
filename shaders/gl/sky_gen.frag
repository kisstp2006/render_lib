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
uniform float uSkyIntensity;

void main()
{
    vec3 dir = normalize(uFaceBasis * vec3(vNdc, 1.0));
    vec3 toSun = normalize(-uSunDirection);

    float h = dir.y;
    vec3 sky = mix(uHorizonColor, uZenithColor, pow(clamp(h, 0.0, 1.0), 0.55));
    vec3 ground = mix(uHorizonColor * 0.85, uGroundColor, pow(clamp(-h, 0.0, 1.0), 0.4));
    vec3 color = (h >= 0.0 ? sky : ground) * uSkyIntensity;

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
