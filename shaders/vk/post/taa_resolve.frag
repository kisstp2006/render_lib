#version 460
#include "../../common/temporal_math.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out float outDepth;

layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 0, binding = 1) uniform sampler2D velocityImage;
layout(set = 0, binding = 2) uniform sampler2D currentDepth;
layout(set = 0, binding = 3) uniform sampler2D historyColor;
layout(set = 0, binding = 4) uniform sampler2D historyDepth;

layout(push_constant) uniform TaaConstants
{
    vec4 Parameters; // history valid, history weight, depth threshold, sharpen
    vec4 Depth;      // near, far
} taa;

void main()
{
    ivec2 size = textureSize(currentColor, 0);
    vec2 texel = 1.0 / vec2(size);
    vec3 current = textureLod(currentColor, uv, 0.0).rgb;
    float depth = textureLod(currentDepth, uv, 0.0).r;
    outDepth = depth;
    vec3 crossAverage = (textureLod(currentColor, uv + vec2(texel.x, 0.0), 0.0).rgb
        + textureLod(currentColor, uv - vec2(texel.x, 0.0), 0.0).rgb
        + textureLod(currentColor, uv + vec2(0.0, texel.y), 0.0).rgb
        + textureLod(currentColor, uv - vec2(0.0, texel.y), 0.0).rgb) * 0.25;
    current = max(current + (current - crossAverage) * max(taa.Parameters.w, 0.0), vec3(0.0));

    vec2 velocity = textureLod(velocityImage, uv, 0.0).rg;
    vec2 historyUv = uv - velocity;
    bool inside = all(greaterThanEqual(historyUv, vec2(0.0)))
               && all(lessThanEqual(historyUv, vec2(1.0)));
    if (taa.Parameters.x < 0.5 || !inside)
    {
        outColor = vec4(current, 1.0);
        return;
    }

    vec3 minimumValue = vec3(1e20), maximumValue = vec3(-1e20);
    vec3 moment1 = vec3(0.0), moment2 = vec3(0.0);
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        vec3 value = EngineRgbToYCoCg(textureLod(currentColor, uv + vec2(x, y) * texel, 0.0).rgb);
        minimumValue = min(minimumValue, value);
        maximumValue = max(maximumValue, value);
        moment1 += value;
        moment2 += value * value;
    }
    vec3 mean = moment1 / 9.0;
    vec3 sigma = sqrt(max(moment2 / 9.0 - mean * mean, vec3(0.0)));
    vec3 history = EngineRgbToYCoCg(textureLod(historyColor, historyUv, 0.0).rgb);
    history = clamp(history, max(minimumValue, mean - sigma * 1.25),
                    min(maximumValue, mean + sigma * 1.25));
    history = EngineYCoCgToRgb(history);

    float previousDepth = textureLod(historyDepth, historyUv, 0.0).r;
    float currentLinearDepth = EngineLinearizeDepth(depth, taa.Depth.x, taa.Depth.y);
    float depthTolerance = max(0.01, currentLinearDepth * taa.Parameters.z);
    float depthConfidence = 1.0 - smoothstep(depthTolerance, depthTolerance * 2.0,
        abs(EngineLinearizeDepth(previousDepth, taa.Depth.x, taa.Depth.y) - currentLinearDepth));
    float motionConfidence = exp(-length(velocity * vec2(size)) * 0.045);
    float skyConfidence = depth > 0.99999 ? 0.82 : 1.0;
    float historyBlend = clamp(taa.Parameters.y, 0.0, 0.98)
                       * depthConfidence * motionConfidence * skyConfidence;
    outColor = vec4(mix(current, history, historyBlend), 1.0);
}
