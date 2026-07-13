#version 460
#include "../../common/color_pipeline.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D inputColor;
layout(push_constant) uniform FxaaConstants
{
    float Subpixel;
    float EdgeThreshold;
    float EdgeThresholdMin;
    float Padding;
} fxaa;

float Luma(vec3 color) { return dot(color, vec3(0.299, 0.587, 0.114)); }

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(inputColor, 0));
    vec3 rgbM = textureLod(inputColor, uv, 0.0).rgb;
    float lumaM = Luma(rgbM);
    float lumaN = Luma(textureLod(inputColor, uv + vec2(0.0, texel.y), 0.0).rgb);
    float lumaS = Luma(textureLod(inputColor, uv - vec2(0.0, texel.y), 0.0).rgb);
    float lumaE = Luma(textureLod(inputColor, uv + vec2(texel.x, 0.0), 0.0).rgb);
    float lumaW = Luma(textureLod(inputColor, uv - vec2(texel.x, 0.0), 0.0).rgb);
    float rangeMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float rangeMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float range = rangeMax - rangeMin;
    if (range < max(fxaa.EdgeThresholdMin, rangeMax * fxaa.EdgeThreshold))
    {
        rgbM += (EngineHash12(gl_FragCoord.xy) - 0.5) / 255.0;
        outColor = vec4(EngineSrgbToLinear(clamp(rgbM, 0.0, 1.0)), 1.0);
        return;
    }

    float lumaNW = Luma(textureLod(inputColor, uv + vec2(-texel.x, texel.y), 0.0).rgb);
    float lumaNE = Luma(textureLod(inputColor, uv + texel, 0.0).rgb);
    float lumaSW = Luma(textureLod(inputColor, uv - texel, 0.0).rgb);
    float lumaSE = Luma(textureLod(inputColor, uv + vec2(texel.x, -texel.y), 0.0).rgb);
    float edgeHorizontal = abs(-2.0 * lumaW + lumaNW + lumaSW)
                         + abs(-2.0 * lumaM + lumaN + lumaS) * 2.0
                         + abs(-2.0 * lumaE + lumaNE + lumaSE);
    float edgeVertical = abs(-2.0 * lumaN + lumaNW + lumaNE)
                       + abs(-2.0 * lumaM + lumaW + lumaE) * 2.0
                       + abs(-2.0 * lumaS + lumaSW + lumaSE);
    bool horizontal = edgeHorizontal >= edgeVertical;
    float lumaNegative = horizontal ? lumaS : lumaW;
    float lumaPositive = horizontal ? lumaN : lumaE;
    float gradientNegative = abs(lumaNegative - lumaM);
    float gradientPositive = abs(lumaPositive - lumaM);
    bool useNegative = gradientNegative >= gradientPositive;
    float gradient = max(gradientNegative, gradientPositive);
    float edgeLuma = (lumaM + (useNegative ? lumaNegative : lumaPositive)) * 0.5;
    vec2 normalStep = horizontal ? vec2(0.0, texel.y) : vec2(texel.x, 0.0);
    if (useNegative) normalStep = -normalStep;
    vec2 edgeUv = uv + normalStep * 0.5;
    vec2 tangentStep = horizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec2 uvNegative = edgeUv - tangentStep;
    vec2 uvPositive = edgeUv + tangentStep;
    float deltaNegative = Luma(textureLod(inputColor, uvNegative, 0.0).rgb) - edgeLuma;
    float deltaPositive = Luma(textureLod(inputColor, uvPositive, 0.0).rgb) - edgeLuma;
    float gradientScaled = gradient * 0.25;
    bool doneNegative = abs(deltaNegative) >= gradientScaled;
    bool donePositive = abs(deltaPositive) >= gradientScaled;
    const float quality[7] = float[7](1.0, 1.5, 2.0, 2.0, 4.0, 8.0, 8.0);
    for (int i = 0; i < 7 && !(doneNegative && donePositive); ++i)
    {
        if (!doneNegative)
        {
            uvNegative -= tangentStep * quality[i];
            deltaNegative = Luma(textureLod(inputColor, uvNegative, 0.0).rgb) - edgeLuma;
            doneNegative = abs(deltaNegative) >= gradientScaled;
        }
        if (!donePositive)
        {
            uvPositive += tangentStep * quality[i];
            deltaPositive = Luma(textureLod(inputColor, uvPositive, 0.0).rgb) - edgeLuma;
            donePositive = abs(deltaPositive) >= gradientScaled;
        }
    }
    float distanceNegative = horizontal ? uv.x - uvNegative.x : uv.y - uvNegative.y;
    float distancePositive = horizontal ? uvPositive.x - uv.x : uvPositive.y - uv.y;
    bool negativeCloser = distanceNegative < distancePositive;
    float nearestDistance = min(distanceNegative, distancePositive);
    float spanLength = max(distanceNegative + distancePositive, 1e-5);
    float edgeOffset = -nearestDistance / spanLength + 0.5;
    float nearestDelta = negativeCloser ? deltaNegative : deltaPositive;
    if ((nearestDelta < 0.0) == (lumaM < edgeLuma)) edgeOffset = 0.0;
    float lumaAverage = (2.0 * (lumaN + lumaS + lumaE + lumaW)
                       + lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    float subpixel = clamp(abs(lumaAverage - lumaM) / max(range, 1e-5), 0.0, 1.0);
    subpixel = subpixel * subpixel * (3.0 - 2.0 * subpixel);
    subpixel = subpixel * subpixel * clamp(fxaa.Subpixel, 0.0, 1.0);
    vec3 color = textureLod(inputColor, uv + normalStep * max(edgeOffset, subpixel), 0.0).rgb;
    color += (EngineHash12(gl_FragCoord.xy) - 0.5) / 255.0;
    outColor = vec4(EngineSrgbToLinear(clamp(color, 0.0, 1.0)), 1.0);
}
