Texture2D<float4> uCurrentColor : register(t0);
SamplerState _uCurrentColor_sampler : register(s0);
Texture2D<float4> uCurrentDepth : register(t2);
SamplerState _uCurrentDepth_sampler : register(s2);
cbuffer EngineGlobals_post_taa_resolve_frag_hlsl : register(b13)
{
    float uSharpen;
    bool uHistoryValid;
    float uNearPlane;
    float uFarPlane;
    float uDepthThreshold;
    float uHistoryWeight;
};

Texture2D<float4> uVelocity : register(t1);
SamplerState _uVelocity_sampler : register(s1);
Texture2D<float4> uHistoryColor : register(t3);
SamplerState _uHistoryColor_sampler : register(s3);
Texture2D<float4> uHistoryDepth : register(t4);
SamplerState _uHistoryDepth_sampler : register(s4);

static float2 vUv;
static float OutDepth;
static float4 OutColor;

struct SPIRV_Cross_Input
{
#ifdef ENGINE_OPENGL
    [[vk::location(1)]]
#endif
    float2 vUv : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 OutColor : SV_Target0;
    float OutDepth : SV_Target1;
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
    int2 size = int2(spvTextureSize(uCurrentColor, uint(0), _121_dummy_parameter));
    float2 texel = 1.0f.xx / float2(size);
    float3 current = uCurrentColor.SampleLevel(_uCurrentColor_sampler, vUv, 0.0f).xyz;
    float currentDepth = uCurrentDepth.SampleLevel(_uCurrentDepth_sampler, vUv, 0.0f).x;
    OutDepth = currentDepth;
    float3 crossAverage = (((uCurrentColor.SampleLevel(_uCurrentColor_sampler, vUv + float2(texel.x, 0.0f), 0.0f).xyz + uCurrentColor.SampleLevel(_uCurrentColor_sampler, vUv - float2(texel.x, 0.0f), 0.0f).xyz) + uCurrentColor.SampleLevel(_uCurrentColor_sampler, vUv + float2(0.0f, texel.y), 0.0f).xyz) + uCurrentColor.SampleLevel(_uCurrentColor_sampler, vUv - float2(0.0f, texel.y), 0.0f).xyz) * 0.25f;
    current = max(current + ((current - crossAverage) * max(uSharpen, 0.0f)), 0.0f.xxx);
    float2 velocity = uVelocity.SampleLevel(_uVelocity_sampler, vUv, 0.0f).xy;
    float2 historyUv = vUv - velocity;
    bool _213 = all(bool2(historyUv.x >= 0.0f.xx.x, historyUv.y >= 0.0f.xx.y));
    bool _220;
    if (_213)
    {
        _220 = all(bool2(historyUv.x <= 1.0f.xx.x, historyUv.y <= 1.0f.xx.y));
    }
    else
    {
        _220 = _213;
    }
    bool inside = _220;
    if ((!uHistoryValid) || (!inside))
    {
        OutColor = float4(current, 1.0f);
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
            float3 param = uCurrentColor.SampleLevel(_uCurrentColor_sampler, vUv + (float2(float(x), float(y)) * texel), 0.0f).xyz;
            float3 sampleValue = EngineRgbToYCoCg(param);
            minimumValue = min(minimumValue, sampleValue);
            maximumValue = max(maximumValue, sampleValue);
            moment1 += sampleValue;
            moment2 += (sampleValue * sampleValue);
        }
    }
    float3 mean = moment1 / 9.0f.xxx;
    float3 sigma = sqrt(max((moment2 / 9.0f.xxx) - (mean * mean), 0.0f.xxx));
    float3 clipMin = max(minimumValue, mean - (sigma * 1.25f));
    float3 clipMax = min(maximumValue, mean + (sigma * 1.25f));
    float3 param_1 = uHistoryColor.SampleLevel(_uHistoryColor_sampler, historyUv, 0.0f).xyz;
    float3 history = EngineRgbToYCoCg(param_1);
    history = clamp(history, clipMin, clipMax);
    float3 param_2 = history;
    history = EngineYCoCgToRgb(param_2);
    float previousDepth = uHistoryDepth.SampleLevel(_uHistoryDepth_sampler, historyUv, 0.0f).x;
    float param_3 = currentDepth;
    float param_4 = uNearPlane;
    float param_5 = uFarPlane;
    float currentLinearDepth = EngineLinearizeDepth(param_3, param_4, param_5);
    float param_6 = previousDepth;
    float param_7 = uNearPlane;
    float param_8 = uFarPlane;
    float previousLinearDepth = EngineLinearizeDepth(param_6, param_7, param_8);
    float depthTolerance = max(0.00999999977648258209228515625f, currentLinearDepth * uDepthThreshold);
    float depthConfidence = 1.0f - smoothstep(depthTolerance, depthTolerance * 2.0f, abs(previousLinearDepth - currentLinearDepth));
    float velocityPixels = length(velocity * float2(size));
    float motionConfidence = exp((-velocityPixels) * 0.04500000178813934326171875f);
    // The sky has a stable camera-derived velocity. Retaining more history
    // prevents the background from exposing the current Halton sample.
    float skyConfidence = (currentDepth > 0.999989986419677734375f) ? 0.959999978542327880859375f : 1.0f;
    float historyBlend = ((clamp(uHistoryWeight, 0.0f, 0.980000019073486328125f) * depthConfidence) * motionConfidence) * skyConfidence;
    OutColor = float4(lerp(current, history, historyBlend.xxx), 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vUv = stage_input.vUv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.OutDepth = OutDepth;
    stage_output.OutColor = OutColor;
    return stage_output;
}
