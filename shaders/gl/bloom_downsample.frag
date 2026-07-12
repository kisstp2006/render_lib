#version 460 core

// 13-tap Jimenez downsample, mirroring VRF's downsample_bloomthreshold
// (same tap pattern and Karis average). Karis weighting + luminance
// threshold only run on the first pass (full res -> half res).

in vec2 vUv;
out vec4 FragColor;

uniform sampler2D uSource;
uniform bool uFirstPass;
uniform float uThreshold;
uniform float uExposure;

const vec3 LUMA = vec3(0.3, 0.59, 0.11);

float Luminance(vec3 c) { return dot(c, LUMA); }
float KarisWeight(vec3 c) { return 1.0 / (1.0 + Luminance(c)); }

vec3 KarisAverage(vec3 a, vec3 b, vec3 c, vec3 d)
{
    float wa = KarisWeight(a);
    float wb = KarisWeight(b);
    float wc = KarisWeight(c);
    float wd = KarisWeight(d);
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

vec3 PlainAverage(vec3 a, vec3 b, vec3 c, vec3 d)
{
    return (a + b + c + d) * 0.25;
}

vec3 ApplyThreshold(vec3 color)
{
    float luma = Luminance(color) * uExposure;
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
        vec3 centerBox = KarisAverage(tl, tr, bl, br);
        vec3 tlBox = KarisAverage(tlf, t, l, center);
        vec3 trBox = KarisAverage(trf, t, r, center);
        vec3 blBox = KarisAverage(blf, b, l, center);
        vec3 brBox = KarisAverage(brf, b, r, center);
        result = centerBox * 0.5 + (tlBox + trBox + blBox + brBox) * 0.125;
        result = ApplyThreshold(result);
    }
    else
    {
        vec3 centerBox = PlainAverage(tl, tr, bl, br);
        vec3 tlBox = PlainAverage(tlf, t, l, center);
        vec3 trBox = PlainAverage(trf, t, r, center);
        vec3 blBox = PlainAverage(blf, b, l, center);
        vec3 brBox = PlainAverage(brf, b, r, center);
        result = centerBox * 0.5 + (tlBox + trBox + blBox + brBox) * 0.125;
    }

    FragColor = vec4(result, 1.0);
}
