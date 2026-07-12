#version 460 core

in vec2 vUv;
layout(location = 0) out vec4 OutColor;
layout(location = 1) out float OutDepth;

uniform sampler2D uCurrentColor;
uniform sampler2D uVelocity;
uniform sampler2D uCurrentDepth;
uniform sampler2D uHistoryColor;
uniform sampler2D uHistoryDepth;
uniform bool uHistoryValid;
uniform float uHistoryWeight;
uniform float uDepthThreshold;
uniform float uSharpen;
uniform float uNearPlane;
uniform float uFarPlane;

float LinearizeDepth(float depth)
{
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * uNearPlane * uFarPlane)
         / max(uFarPlane + uNearPlane - ndc * (uFarPlane - uNearPlane), 1e-6);
}

vec3 RgbToYCoCg(vec3 c)
{
    return vec3(c.r * 0.25 + c.g * 0.5 + c.b * 0.25,
                c.r * 0.5 - c.b * 0.5,
               -c.r * 0.25 + c.g * 0.5 - c.b * 0.25);
}

vec3 YCoCgToRgb(vec3 c)
{
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

void main()
{
    ivec2 size = textureSize(uCurrentColor, 0);
    vec2 texel = 1.0 / vec2(size);
    vec3 current = textureLod(uCurrentColor, vUv, 0.0).rgb;
    float currentDepth = textureLod(uCurrentDepth, vUv, 0.0).r;
    OutDepth = currentDepth;

    // A small unsharp mask restores detail softened by temporal integration.
    vec3 crossAverage = (
        textureLod(uCurrentColor, vUv + vec2(texel.x, 0.0), 0.0).rgb
      + textureLod(uCurrentColor, vUv - vec2(texel.x, 0.0), 0.0).rgb
      + textureLod(uCurrentColor, vUv + vec2(0.0, texel.y), 0.0).rgb
      + textureLod(uCurrentColor, vUv - vec2(0.0, texel.y), 0.0).rgb) * 0.25;
    current = max(current + (current - crossAverage) * max(uSharpen, 0.0), vec3(0.0));

    vec2 velocity = textureLod(uVelocity, vUv, 0.0).rg;
    vec2 historyUv = vUv - velocity;
    bool inside = all(greaterThanEqual(historyUv, vec2(0.0)))
               && all(lessThanEqual(historyUv, vec2(1.0)));
    if (!uHistoryValid || !inside)
    {
        OutColor = vec4(current, 1.0);
        return;
    }

    // Variance clipping in YCoCg preserves chroma better than an RGB AABB and
    // rejects stale history around moving silhouettes and newly exposed pixels.
    vec3 minimumValue = vec3(1e20);
    vec3 maximumValue = vec3(-1e20);
    vec3 moment1 = vec3(0.0);
    vec3 moment2 = vec3(0.0);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec3 sampleValue = RgbToYCoCg(textureLod(uCurrentColor, vUv + vec2(x, y) * texel, 0.0).rgb);
            minimumValue = min(minimumValue, sampleValue);
            maximumValue = max(maximumValue, sampleValue);
            moment1 += sampleValue;
            moment2 += sampleValue * sampleValue;
        }
    }
    vec3 mean = moment1 / 9.0;
    vec3 sigma = sqrt(max(moment2 / 9.0 - mean * mean, vec3(0.0)));
    vec3 clipMin = max(minimumValue, mean - sigma * 1.25);
    vec3 clipMax = min(maximumValue, mean + sigma * 1.25);

    vec3 history = RgbToYCoCg(textureLod(uHistoryColor, historyUv, 0.0).rgb);
    history = clamp(history, clipMin, clipMax);
    history = YCoCgToRgb(history);

    float previousDepth = textureLod(uHistoryDepth, historyUv, 0.0).r;
    float currentLinearDepth = LinearizeDepth(currentDepth);
    float previousLinearDepth = LinearizeDepth(previousDepth);
    float depthTolerance = max(0.01, currentLinearDepth * uDepthThreshold);
    float depthConfidence = 1.0 - smoothstep(depthTolerance, depthTolerance * 2.0,
                                             abs(previousLinearDepth - currentLinearDepth));
    float velocityPixels = length(velocity * vec2(size));
    float motionConfidence = exp(-velocityPixels * 0.045);
    // The procedural sky changes independently from scene geometry; keep a
    // little less history at the far plane to avoid star/sky trails.
    float skyConfidence = currentDepth > 0.99999 ? 0.82 : 1.0;
    float historyBlend = clamp(uHistoryWeight, 0.0, 0.98)
                       * depthConfidence * motionConfidence * skyConfidence;
    OutColor = vec4(mix(current, history, historyBlend), 1.0);
}
