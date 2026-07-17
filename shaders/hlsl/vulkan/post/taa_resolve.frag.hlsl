#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer TaaConstants
{
    float4 taa_Parameters : packoffset(c0);
    float4 taa_Depth : packoffset(c1);
};

Texture2D<float4> currentColor : register(t0, space0);
SamplerState _currentColor_sampler : register(s0, space0);
Texture2D<float4> currentDepth : register(t2, space0);
SamplerState _currentDepth_sampler : register(s2, space0);
Texture2D<float4> velocityImage : register(t1, space0);
SamplerState _velocityImage_sampler : register(s1, space0);
Texture2D<float4> historyColor : register(t3, space0);
SamplerState _historyColor_sampler : register(s3, space0);
Texture2D<float4> historyDepth : register(t4, space0);
SamplerState _historyDepth_sampler : register(s4, space0);

static float2 uv;
static float outDepth;
static float4 outColor;

struct SPIRV_Cross_Input
{
    float2 uv : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 outColor : SV_Target0;
    float outDepth : SV_Target1;
};

uint2 spvTextureSize(Texture2D<float4> Tex, uint Level, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(Level, ret.x, ret.y, Param);
    return ret;
}

float3 EngineRgbToYCoCg(float3 color)
{
    return float3(((color.x * 0.25f) + (color.y * 0.5f)) + (color.z * 0.25f), (color.x * 0.5f) - (color.z * 0.5f), (((-color.x) * 0.25f) + (color.y * 0.5f)) - (color.z * 0.25f));
}

float3 EngineYCoCgToRgb(float3 color)
{
    return float3((color.x + color.y) - color.z, color.x + color.z, (color.x - color.y) - color.z);
}

float EngineLinearizeDepth(float depth, float nearPlane, float farPlane)
{
    float ndc = (depth * 2.0f) - 1.0f;
    return ((2.0f * nearPlane) * farPlane) / max((farPlane + nearPlane) - (ndc * (farPlane - nearPlane)), 9.9999999747524270787835121154785e-07f);
}

void frag_main()
{
    uint _121_dummy_parameter;
    int2 size = int2(spvTextureSize(currentColor, uint(0), _121_dummy_parameter));
    float2 texel = 1.0f.xx / float2(size);
    float3 current = currentColor.SampleLevel(_currentColor_sampler, uv, 0.0f).xyz;
    float depth = currentDepth.SampleLevel(_currentDepth_sampler, uv, 0.0f).x;
    outDepth = depth;
    float3 crossAverage = (((currentColor.SampleLevel(_currentColor_sampler, uv + float2(texel.x, 0.0f), 0.0f).xyz + currentColor.SampleLevel(_currentColor_sampler, uv - float2(texel.x, 0.0f), 0.0f).xyz) + currentColor.SampleLevel(_currentColor_sampler, uv + float2(0.0f, texel.y), 0.0f).xyz) + currentColor.SampleLevel(_currentColor_sampler, uv - float2(0.0f, texel.y), 0.0f).xyz) * 0.25f;
    current = max(current + ((current - crossAverage) * max(taa_Parameters.w, 0.0f)), 0.0f.xxx);
    float2 velocity = velocityImage.SampleLevel(_velocityImage_sampler, uv, 0.0f).xy;
    float2 historyUv = uv - velocity;
    bool _217 = all(bool2(historyUv.x >= 0.0f.xx.x, historyUv.y >= 0.0f.xx.y));
    bool _224;
    if (_217)
    {
        _224 = all(bool2(historyUv.x <= 1.0f.xx.x, historyUv.y <= 1.0f.xx.y));
    }
    else
    {
        _224 = _217;
    }
    bool inside = _224;
    if ((taa_Parameters.x < 0.5f) || (!inside))
    {
        outColor = float4(current, 1.0f);
        return;
    }
    float3 minimumValue = 100000002004087734272.0f.xxx;
    float3 maximumValue = (-100000002004087734272.0f).xxx;
    float3 moment1 = 0.0f.xxx;
    float3 moment2 = 0.0f.xxx;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float3 param = currentColor.SampleLevel(_currentColor_sampler, uv + (float2(float(x), float(y)) * texel), 0.0f).xyz;
            float3 value = EngineRgbToYCoCg(param);
            minimumValue = min(minimumValue, value);
            maximumValue = max(maximumValue, value);
            moment1 += value;
            moment2 += (value * value);
        }
    }
    float3 mean = moment1 / 9.0f.xxx;
    float3 sigma = sqrt(max((moment2 / 9.0f.xxx) - (mean * mean), 0.0f.xxx));
    float3 param_1 = historyColor.SampleLevel(_historyColor_sampler, historyUv, 0.0f).xyz;
    float3 history = EngineRgbToYCoCg(param_1);
    history = clamp(history, max(minimumValue, mean - (sigma * 1.25f)), min(maximumValue, mean + (sigma * 1.25f)));
    float3 param_2 = history;
    history = EngineYCoCgToRgb(param_2);
    float previousDepth = historyDepth.SampleLevel(_historyDepth_sampler, historyUv, 0.0f).x;
    float param_3 = depth;
    float param_4 = taa_Depth.x;
    float param_5 = taa_Depth.y;
    float currentLinearDepth = EngineLinearizeDepth(param_3, param_4, param_5);
    float depthTolerance = max(0.00999999977648258209228515625f, currentLinearDepth * taa_Parameters.z);
    float param_6 = previousDepth;
    float param_7 = taa_Depth.x;
    float param_8 = taa_Depth.y;
    float depthConfidence = 1.0f - smoothstep(depthTolerance, depthTolerance * 2.0f, abs(EngineLinearizeDepth(param_6, param_7, param_8) - currentLinearDepth));
    float motionConfidence = exp((-length(velocity * float2(size))) * 0.04500000178813934326171875f);
    // The sky has a stable camera-derived velocity. Retaining more history
    // prevents the background from exposing the current Halton sample.
    float skyConfidence = (depth > 0.999989986419677734375f) ? 0.959999978542327880859375f : 1.0f;
    float historyBlend = ((clamp(taa_Parameters.y, 0.0f, 0.980000019073486328125f) * depthConfidence) * motionConfidence) * skyConfidence;
    outColor = float4(lerp(current, history, historyBlend.xxx), 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    uv = stage_input.uv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.outDepth = outDepth;
    stage_output.outColor = outColor;
    return stage_output;
}
