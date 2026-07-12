#version 460 core

// Final post pass, mirroring VRF's post_processing.frag.slang: exposure
// scale, ADD-mode bloom composite, Uncharted tonemapper with Source's
// shoulder/linear/toe parameterization + precomputed white point scale,
// exact linear->sRGB conversion, and a sub-precision dither to break up
// banding (hash noise standing in for Valve's blue-noise texture).

in vec2 vUv;
out vec4 FragColor;

uniform sampler2D uSceneColor;
uniform sampler2D uBloom;

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

vec3 TonemapColor(vec3 c)
{
    vec3 num = c * (uShoulderStrength * c + uLinearStrength * uLinearAngle) + uToeNumerator * uToeStrength;
    vec3 den = c * (uShoulderStrength * c + uLinearStrength) + uToeDenominator * uToeStrength;
    vec3 mapped = num / den - uToeNumerator / uToeDenominator;
    return mapped * uWhitePointScale;
}

vec3 SrgbLinearToGamma(vec3 c)
{
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    vec3 color = textureLod(uSceneColor, vUv, 0.0).rgb;

    if (uPostEnabled)
    {
        color *= uExposure;
        color += textureLod(uBloom, vUv, 0.0).rgb * uBloomStrength;

        color = TonemapColor(color);
        color = SrgbLinearToGamma(clamp(color, 0.0, 1.0));

        color += (Hash12(gl_FragCoord.xy) - 0.5) / 255.0;
    }
    else
    {
        color = SrgbLinearToGamma(clamp(color, 0.0, 1.0));
    }

    FragColor = vec4(color, 1.0);
}
