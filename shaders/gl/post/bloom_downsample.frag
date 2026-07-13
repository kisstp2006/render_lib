#version 460 core
#include "../../common/bloom_math.glsl"

// 13-tap Jimenez downsample, mirroring VRF's downsample_bloomthreshold
// (same tap pattern and Karis average). Karis weighting + luminance
// threshold only run on the first pass (full res -> half res).

in vec2 vUv;
out vec4 FragColor;

uniform sampler2D uSource;
uniform bool uFirstPass;
uniform float uThreshold;
uniform float uExposure;

vec3 ApplyThreshold(vec3 color)
{
    float luma = EngineBloomLuminance(color) * uExposure;
    float weight = clamp((luma - uThreshold) / max(luma, 1e-4), 0.0, 1.0);
    return color * weight;
}

void main()
{
    vec3 center = textureLod(uSource, vUv, 0.0).rgb;

    vec3 tl = textureLodOffset(uSource, vUv, 0.0, ivec2(-1, -1)).rgb;
    vec3 tr = textureLodOffset(uSource, vUv, 0.0, ivec2(1, -1)).rgb;
    vec3 bl = textureLodOffset(uSource, vUv, 0.0, ivec2(-1, 1)).rgb;
    vec3 br = textureLodOffset(uSource, vUv, 0.0, ivec2(1, 1)).rgb;

    vec3 t = textureLodOffset(uSource, vUv, 0.0, ivec2(0, -2)).rgb;
    vec3 l = textureLodOffset(uSource, vUv, 0.0, ivec2(-2, 0)).rgb;
    vec3 r = textureLodOffset(uSource, vUv, 0.0, ivec2(2, 0)).rgb;
    vec3 b = textureLodOffset(uSource, vUv, 0.0, ivec2(0, 2)).rgb;

    vec3 tlf = textureLodOffset(uSource, vUv, 0.0, ivec2(-2, -2)).rgb;
    vec3 trf = textureLodOffset(uSource, vUv, 0.0, ivec2(2, -2)).rgb;
    vec3 blf = textureLodOffset(uSource, vUv, 0.0, ivec2(-2, 2)).rgb;
    vec3 brf = textureLodOffset(uSource, vUv, 0.0, ivec2(2, 2)).rgb;

    vec3 result;
    if (uFirstPass)
    {
        vec3 centerBox = EngineBloomAverage(tl, tr, bl, br, true);
        vec3 tlBox = EngineBloomAverage(tlf, t, l, center, true);
        vec3 trBox = EngineBloomAverage(trf, t, r, center, true);
        vec3 blBox = EngineBloomAverage(blf, b, l, center, true);
        vec3 brBox = EngineBloomAverage(brf, b, r, center, true);
        result = centerBox * 0.5 + (tlBox + trBox + blBox + brBox) * 0.125;
        result = ApplyThreshold(result);
    }
    else
    {
        vec3 centerBox = EngineBloomAverage(tl, tr, bl, br, false);
        vec3 tlBox = EngineBloomAverage(tlf, t, l, center, false);
        vec3 trBox = EngineBloomAverage(trf, t, r, center, false);
        vec3 blBox = EngineBloomAverage(blf, b, l, center, false);
        vec3 brBox = EngineBloomAverage(brf, b, r, center, false);
        result = centerBox * 0.5 + (tlBox + trBox + blBox + brBox) * 0.125;
    }

    FragColor = vec4(result, 1.0);
}
