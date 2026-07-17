static const float _431[7] = { 1.0f, 1.5f, 2.0f, 2.0f, 4.0f, 8.0f, 8.0f };

Texture2D<float4> uColor : register(t0);
SamplerState _uColor_sampler : register(s0);
cbuffer EngineGlobals_post_fxaa_frag_hlsl : register(b13)
{
    float uEdgeThresholdMin;
    float uEdgeThreshold;
    float uSubpixel;
};


static float4 gl_FragCoord;
static float2 vUv;
static float4 FragColor;

struct SPIRV_Cross_Input
{
#ifdef ENGINE_OPENGL
    [[vk::location(1)]]
#endif
    float2 vUv : TEXCOORD1;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
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

void frag_main()
{
    uint _70_dummy_parameter;
    float2 texel = 1.0f.xx / float2(int2(spvTextureSize(uColor, uint(0), _70_dummy_parameter)));
    float3 rgbM = uColor.SampleLevel(_uColor_sampler, vUv, 0.0f).xyz;
    float3 param = rgbM;
    float lumaM = Luma(param);
    float3 param_1 = uColor.SampleLevel(_uColor_sampler, vUv + float2(0.0f, texel.y), 0.0f).xyz;
    float lumaN = Luma(param_1);
    float3 param_2 = uColor.SampleLevel(_uColor_sampler, vUv - float2(0.0f, texel.y), 0.0f).xyz;
    float lumaS = Luma(param_2);
    float3 param_3 = uColor.SampleLevel(_uColor_sampler, vUv + float2(texel.x, 0.0f), 0.0f).xyz;
    float lumaE = Luma(param_3);
    float3 param_4 = uColor.SampleLevel(_uColor_sampler, vUv - float2(texel.x, 0.0f), 0.0f).xyz;
    float lumaW = Luma(param_4);
    float rangeMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float rangeMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float range = rangeMax - rangeMin;
    if (range < max(uEdgeThresholdMin, rangeMax * uEdgeThreshold))
    {
        float2 param_5 = gl_FragCoord.xy;
        rgbM += ((EngineHash12(param_5) - 0.5f) / 255.0f).xxx;
        FragColor = float4(rgbM, 1.0f);
        return;
    }
    float3 param_6 = uColor.SampleLevel(_uColor_sampler, vUv + float2(-texel.x, texel.y), 0.0f).xyz;
    float lumaNW = Luma(param_6);
    float3 param_7 = uColor.SampleLevel(_uColor_sampler, vUv + texel, 0.0f).xyz;
    float lumaNE = Luma(param_7);
    float3 param_8 = uColor.SampleLevel(_uColor_sampler, vUv - texel, 0.0f).xyz;
    float lumaSW = Luma(param_8);
    float3 param_9 = uColor.SampleLevel(_uColor_sampler, vUv + float2(texel.x, -texel.y), 0.0f).xyz;
    float lumaSE = Luma(param_9);
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
    float2 _330;
    if (horizontal)
    {
        _330 = float2(0.0f, texel.y);
    }
    else
    {
        _330 = float2(texel.x, 0.0f);
    }
    float2 normalStep = _330;
    if (useNegative)
    {
        normalStep = -normalStep;
    }
    float2 edgeUv = vUv + (normalStep * 0.5f);
    float2 _353;
    if (horizontal)
    {
        _353 = float2(texel.x, 0.0f);
    }
    else
    {
        _353 = float2(0.0f, texel.y);
    }
    float2 tangentStep = _353;
    float2 uvNegative = edgeUv - tangentStep;
    float2 uvPositive = edgeUv + tangentStep;
    float3 param_10 = uColor.SampleLevel(_uColor_sampler, uvNegative, 0.0f).xyz;
    float deltaNegative = Luma(param_10) - edgeLuma;
    float3 param_11 = uColor.SampleLevel(_uColor_sampler, uvPositive, 0.0f).xyz;
    float deltaPositive = Luma(param_11) - edgeLuma;
    float gradientScaled = gradient * 0.25f;
    bool doneNegative = abs(deltaNegative) >= gradientScaled;
    bool donePositive = abs(deltaPositive) >= gradientScaled;
    int i = 0;
    for (;;)
    {
        bool _413 = i < 7;
        bool _420;
        if (_413)
        {
            _420 = !(doneNegative && donePositive);
        }
        else
        {
            _420 = _413;
        }
        if (_420)
        {
            if (!doneNegative)
            {
                uvNegative -= (tangentStep * _431[i]);
                float3 param_12 = uColor.SampleLevel(_uColor_sampler, uvNegative, 0.0f).xyz;
                deltaNegative = Luma(param_12) - edgeLuma;
                doneNegative = abs(deltaNegative) >= gradientScaled;
            }
            if (!donePositive)
            {
                uvPositive += (tangentStep * _431[i]);
                float3 param_13 = uColor.SampleLevel(_uColor_sampler, uvPositive, 0.0f).xyz;
                deltaPositive = Luma(param_13) - edgeLuma;
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
    float _481;
    if (horizontal)
    {
        _481 = vUv.x - uvNegative.x;
    }
    else
    {
        _481 = vUv.y - uvNegative.y;
    }
    float distanceNegative = _481;
    float _499;
    if (horizontal)
    {
        _499 = uvPositive.x - vUv.x;
    }
    else
    {
        _499 = uvPositive.y - vUv.y;
    }
    float distancePositive = _499;
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
    subpixel = (subpixel * subpixel) * clamp(uSubpixel, 0.0f, 1.0f);
    float finalOffset = max(edgeOffset, subpixel);
    float3 color = uColor.SampleLevel(_uColor_sampler, vUv + (normalStep * finalOffset), 0.0f).xyz;
    float2 param_14 = gl_FragCoord.xy;
    color += ((EngineHash12(param_14) - 0.5f) / 255.0f).xxx;
    FragColor = float4(color, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    vUv = stage_input.vUv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
