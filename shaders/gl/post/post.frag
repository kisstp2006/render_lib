#version 460 core
#include "../../common/color_pipeline.glsl"

// Final post pass, mirroring VRF's post_processing.frag.slang: exposure
// scale, ADD-mode bloom composite, Uncharted tonemapper with Source's
// shoulder/linear/toe parameterization + precomputed white point scale,
// exact linear->sRGB conversion, and a sub-precision dither to break up
// banding (hash noise standing in for Valve's blue-noise texture).

in vec2 vUv;
out vec4 FragColor;

uniform sampler2D uSceneColor;
uniform sampler2D uBloom;
uniform sampler3D uColorLut;

uniform bool uPostEnabled;
uniform float uExposure;
uniform float uBloomStrength;

uniform float uShoulderStrength;
uniform float uLinearStrength;
uniform float uLinearAngle;
uniform float uToeStrength;
uniform float uToeNumerator;
uniform float uToeDenominator;
uniform float uWhitePointScale; // 1 / Tonemap(WhitePoint), computed on CPU like VRF

// LDR grade after tonemap, followed by the Source/VRF-style 3D LUT.
uniform float uSaturation;
uniform float uContrast;
uniform vec3 uColorTint;
uniform float uColorLutWeight;
uniform vec3 uColorLutDomainMin;
uniform vec3 uColorLutDomainMax;
uniform float uColorLutSize;
uniform bool uDitherEnabled;

void main()
{
    vec3 color = textureLod(uSceneColor, vUv, 0.0).rgb;

    if (uPostEnabled)
    {
        color *= uExposure;
        color += textureLod(uBloom, vUv, 0.0).rgb * uBloomStrength;

        color = EngineUnchartedTonemap(
            color, vec4(uShoulderStrength, uLinearStrength, uLinearAngle, uToeStrength),
            vec3(uToeNumerator, uToeDenominator, uWhitePointScale));
        color = EngineLinearToSrgb(clamp(color, 0.0, 1.0));

        // Grade in display space, where Source's color correction LUT lives
        float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(vec3(luma), color, uSaturation);
        color = (color - 0.5) * uContrast + 0.5;
        color = clamp(color * uColorTint, 0.0, 1.0);

        if (uColorLutWeight > 0.0)
        {
            vec3 domainColor = clamp((color - uColorLutDomainMin)
                                   / max(uColorLutDomainMax - uColorLutDomainMin, vec3(1e-6)), 0.0, 1.0);
            // Source 2/VRF half-texel addressing: for a 32^3 LUT this is
            // color * 0.96875 + 0.015625.
            vec3 lutUv = domainColor * ((uColorLutSize - 1.0) / uColorLutSize)
                       + 0.5 / uColorLutSize;
            vec3 graded = textureLod(uColorLut, lutUv, 0.0).rgb;
            color = mix(color, graded, clamp(uColorLutWeight, 0.0, 1.0));
        }

        if (uDitherEnabled)
            color += (EngineHash12(gl_FragCoord.xy) - 0.5) / 255.0;
    }
    else
    {
        color = EngineLinearToSrgb(clamp(color, 0.0, 1.0));
    }

    FragColor = vec4(color, 1.0);
}
