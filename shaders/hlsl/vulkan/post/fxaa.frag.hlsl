static const float _472[7] = { 1.0f, 1.5f, 2.0f, 2.0f, 4.0f, 8.0f, 8.0f };

#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer FxaaConstants
{
    float fxaa_Subpixel : packoffset(c0);
    float fxaa_EdgeThreshold : packoffset(c0.y);
    float fxaa_EdgeThresholdMin : packoffset(c0.z);
    float fxaa_Padding : packoffset(c0.w);
};

Texture2D<float4> inputColor : register(t0, space0);
SamplerState _inputColor_sampler : register(s0, space0);

static float4 gl_FragCoord;
static float2 uv;
static float4 outColor;

struct SPIRV_Cross_Input
{
    float2 uv : TEXCOORD0;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 outColor : SV_Target0;
};

uint2 spvTextureSize(Texture2D<float4> Tex, uint Level, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(Level, ret.x, ret.y, Param);
    return ret;
}

float Luma(float3 color)
{
    return dot(color, float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
}

float EngineHash12(float2 position)
{
    float3 p3 = frac(position.xyx * 0.103100001811981201171875f);
    p3 += dot(p3, p3.yzx + 33.3300018310546875f.xxx).xxx;
    return frac((p3.x + p3.y) * p3.z);
}

float3 EngineSrgbToLinear(float3 color)
{
    float3 low = color / 12.9200000762939453125f.xxx;
    float3 high = pow((max(color, 0.0f.xxx) + 0.054999999701976776123046875f.xxx) / 1.05499994754791259765625f.xxx, 2.400000095367431640625f.xxx);
    return lerp(low, high, step(0.040449999272823333740234375f.xxx, color));
}

void frag_main()
{
    uint _102_dummy_parameter;
    float2 texel = 1.0f.xx / float2(int2(spvTextureSize(inputColor, uint(0), _102_dummy_parameter)));
    float3 rgbM = inputColor.SampleLevel(_inputColor_sampler, uv, 0.0f).xyz;
    float3 param = rgbM;
    float lumaM = Luma(param);
    float3 param_1 = inputColor.SampleLevel(_inputColor_sampler, uv + float2(0.0f, texel.y), 0.0f).xyz;
    float lumaN = Luma(param_1);
    float3 param_2 = inputColor.SampleLevel(_inputColor_sampler, uv - float2(0.0f, texel.y), 0.0f).xyz;
    float lumaS = Luma(param_2);
    float3 param_3 = inputColor.SampleLevel(_inputColor_sampler, uv + float2(texel.x, 0.0f), 0.0f).xyz;
    float lumaE = Luma(param_3);
    float3 param_4 = inputColor.SampleLevel(_inputColor_sampler, uv - float2(texel.x, 0.0f), 0.0f).xyz;
    float lumaW = Luma(param_4);
    float rangeMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float rangeMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float range = rangeMax - rangeMin;
    if (range < max(fxaa_EdgeThresholdMin, rangeMax * fxaa_EdgeThreshold))
    {
        float2 param_5 = gl_FragCoord.xy;
        rgbM += ((EngineHash12(param_5) - 0.5f) / 255.0f).xxx;
        float3 param_6 = clamp(rgbM, 0.0f.xxx, 1.0f.xxx);
        outColor = float4(EngineSrgbToLinear(param_6), 1.0f);
        return;
    }
    float3 param_7 = inputColor.SampleLevel(_inputColor_sampler, uv + float2(-texel.x, texel.y), 0.0f).xyz;
    float lumaNW = Luma(param_7);
    float3 param_8 = inputColor.SampleLevel(_inputColor_sampler, uv + texel, 0.0f).xyz;
    float lumaNE = Luma(param_8);
    float3 param_9 = inputColor.SampleLevel(_inputColor_sampler, uv - texel, 0.0f).xyz;
    float lumaSW = Luma(param_9);
    float3 param_10 = inputColor.SampleLevel(_inputColor_sampler, uv + float2(texel.x, -texel.y), 0.0f).xyz;
    float lumaSE = Luma(param_10);
    float edgeHorizontal = (abs((((-2.0f) * lumaW) + lumaNW) + lumaSW) + (abs((((-2.0f) * lumaM) + lumaN) + lumaS) * 2.0f)) + abs((((-2.0f) * lumaE) + lumaNE) + lumaSE);
    float edgeVertical = (abs((((-2.0f) * lumaN) + lumaNW) + lumaNE) + (abs((((-2.0f) * lumaM) + lumaW) + lumaE) * 2.0f)) + abs((((-2.0f) * lumaS) + lumaSW) + lumaSE);
    bool horizontal = edgeHorizontal >= edgeVertical;
    float lumaNegative = horizontal ? lumaS : lumaW;
    float lumaPositive = horizontal ? lumaN : lumaE;
    float gradientNegative = abs(lumaNegative - lumaM);
    float gradientPositive = abs(lumaPositive - lumaM);
    bool useNegative = gradientNegative >= gradientPositive;
    float gradient = max(gradientNegative, gradientPositive);
    float edgeLuma = (lumaM + (useNegative ? lumaNegative : lumaPositive)) * 0.5f;
    float2 _371;
    if (horizontal)
    {
        _371 = float2(0.0f, texel.y);
    }
    else
    {
        _371 = float2(texel.x, 0.0f);
    }
    float2 normalStep = _371;
    if (useNegative)
    {
        normalStep = -normalStep;
    }
    float2 edgeUv = uv + (normalStep * 0.5f);
    float2 _394;
    if (horizontal)
    {
        _394 = float2(texel.x, 0.0f);
    }
    else
    {
        _394 = float2(0.0f, texel.y);
    }
    float2 tangentStep = _394;
    float2 uvNegative = edgeUv - tangentStep;
    float2 uvPositive = edgeUv + tangentStep;
    float3 param_11 = inputColor.SampleLevel(_inputColor_sampler, uvNegative, 0.0f).xyz;
    float deltaNegative = Luma(param_11) - edgeLuma;
    float3 param_12 = inputColor.SampleLevel(_inputColor_sampler, uvPositive, 0.0f).xyz;
    float deltaPositive = Luma(param_12) - edgeLuma;
    float gradientScaled = gradient * 0.25f;
    bool doneNegative = abs(deltaNegative) >= gradientScaled;
    bool donePositive = abs(deltaPositive) >= gradientScaled;
    int i = 0;
    for (;;)
    {
        bool _454 = i < 7;
        bool _461;
        if (_454)
        {
            _461 = !(doneNegative && donePositive);
        }
        else
        {
            _461 = _454;
        }
        if (_461)
        {
            if (!doneNegative)
            {
                uvNegative -= (tangentStep * _472[i]);
                float3 param_13 = inputColor.SampleLevel(_inputColor_sampler, uvNegative, 0.0f).xyz;
                deltaNegative = Luma(param_13) - edgeLuma;
                doneNegative = abs(deltaNegative) >= gradientScaled;
            }
            if (!donePositive)
            {
                uvPositive += (tangentStep * _472[i]);
                float3 param_14 = inputColor.SampleLevel(_inputColor_sampler, uvPositive, 0.0f).xyz;
                deltaPositive = Luma(param_14) - edgeLuma;
                donePositive = abs(deltaPositive) >= gradientScaled;
            }
            i++;
            continue;
        }
        else
        {
            break;
        }
    }
    float _521;
    if (horizontal)
    {
        _521 = uv.x - uvNegative.x;
    }
    else
    {
        _521 = uv.y - uvNegative.y;
    }
    float distanceNegative = _521;
    float _539;
    if (horizontal)
    {
        _539 = uvPositive.x - uv.x;
    }
    else
    {
        _539 = uvPositive.y - uv.y;
    }
    float distancePositive = _539;
    bool negativeCloser = distanceNegative < distancePositive;
    float nearestDistance = min(distanceNegative, distancePositive);
    float spanLength = max(distanceNegative + distancePositive, 9.9999997473787516355514526367188e-06f);
    float edgeOffset = ((-nearestDistance) / spanLength) + 0.5f;
    float nearestDelta = negativeCloser ? deltaNegative : deltaPositive;
    if ((nearestDelta < 0.0f) == (lumaM < edgeLuma))
    {
        edgeOffset = 0.0f;
    }
    float lumaAverage = (((((2.0f * (((lumaN + lumaS) + lumaE) + lumaW)) + lumaNW) + lumaNE) + lumaSW) + lumaSE) / 12.0f;
    float subpixel = clamp(abs(lumaAverage - lumaM) / max(range, 9.9999997473787516355514526367188e-06f), 0.0f, 1.0f);
    subpixel = (subpixel * subpixel) * (3.0f - (2.0f * subpixel));
    subpixel = (subpixel * subpixel) * clamp(fxaa_Subpixel, 0.0f, 1.0f);
    float3 color = inputColor.SampleLevel(_inputColor_sampler, uv + (normalStep * max(edgeOffset, subpixel)), 0.0f).xyz;
    float2 param_15 = gl_FragCoord.xy;
    color += ((EngineHash12(param_15) - 0.5f) / 255.0f).xxx;
    float3 param_16 = clamp(color, 0.0f.xxx, 1.0f.xxx);
    outColor = float4(EngineSrgbToLinear(param_16), 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    uv = stage_input.uv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.outColor = outColor;
    return stage_output;
}
