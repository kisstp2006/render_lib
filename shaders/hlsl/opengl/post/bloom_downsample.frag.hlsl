cbuffer EngineGlobals_post_bloom_downsample_frag_hlsl : register(b13)
{
    float uExposure;
    float uThreshold;
    bool uFirstPass;
};

Texture2D<float4> uSource : register(t0);
SamplerState _uSource_sampler : register(s0);

static float2 vUv;
static float4 FragColor;

struct SPIRV_Cross_Input
{
#ifdef ENGINE_OPENGL
    [[vk::location(1)]]
#endif
    float2 vUv : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float EngineBloomLuminance(float3 color)
{
    return dot(color, float3(0.300000011920928955078125f, 0.589999973773956298828125f, 0.10999999940395355224609375f));
}

float3 EngineBloomAverage(float3 a, float3 b, float3 c, float3 d, bool karis)
{
    if (!karis)
    {
        return (((a + b) + c) + d) * 0.25f;
    }
    float3 param = a;
    float wa = 1.0f / (1.0f + EngineBloomLuminance(param));
    float3 param_1 = b;
    float wb = 1.0f / (1.0f + EngineBloomLuminance(param_1));
    float3 param_2 = c;
    float wc = 1.0f / (1.0f + EngineBloomLuminance(param_2));
    float3 param_3 = d;
    float wd = 1.0f / (1.0f + EngineBloomLuminance(param_3));
    return ((((a * wa) + (b * wb)) + (c * wc)) + (d * wd)) / (((wa + wb) + wc) + wd).xxx;
}

float3 ApplyThreshold(float3 color)
{
    float3 param = color;
    float luma = EngineBloomLuminance(param) * uExposure;
    float weight = clamp((luma - uThreshold) / max(luma, 9.9999997473787516355514526367188e-05f), 0.0f, 1.0f);
    return color * weight;
}

void frag_main()
{
    float3 center = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f).xyz;
    float3 tl = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(-1, -1)).xyz;
    float3 tr = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(1, -1)).xyz;
    float3 bl = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(-1, 1)).xyz;
    float3 br = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(1, 1)).xyz;
    float3 t = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(0, -2)).xyz;
    float3 l = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(-2, 0)).xyz;
    float3 r = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(2, 0)).xyz;
    float3 b = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(0, 2)).xyz;
    float3 tlf = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(-2, -2)).xyz;
    float3 trf = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(2, -2)).xyz;
    float3 blf = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(-2, 2)).xyz;
    float3 brf = uSource.SampleLevel(_uSource_sampler, vUv, 0.0f, int2(2, 2)).xyz;
    float3 result;
    if (uFirstPass)
    {
        float3 param = tl;
        float3 param_1 = tr;
        float3 param_2 = bl;
        float3 param_3 = br;
        bool param_4 = true;
        float3 centerBox = EngineBloomAverage(param, param_1, param_2, param_3, param_4);
        float3 param_5 = tlf;
        float3 param_6 = t;
        float3 param_7 = l;
        float3 param_8 = center;
        bool param_9 = true;
        float3 tlBox = EngineBloomAverage(param_5, param_6, param_7, param_8, param_9);
        float3 param_10 = trf;
        float3 param_11 = t;
        float3 param_12 = r;
        float3 param_13 = center;
        bool param_14 = true;
        float3 trBox = EngineBloomAverage(param_10, param_11, param_12, param_13, param_14);
        float3 param_15 = blf;
        float3 param_16 = b;
        float3 param_17 = l;
        float3 param_18 = center;
        bool param_19 = true;
        float3 blBox = EngineBloomAverage(param_15, param_16, param_17, param_18, param_19);
        float3 param_20 = brf;
        float3 param_21 = b;
        float3 param_22 = r;
        float3 param_23 = center;
        bool param_24 = true;
        float3 brBox = EngineBloomAverage(param_20, param_21, param_22, param_23, param_24);
        result = (centerBox * 0.5f) + ((((tlBox + trBox) + blBox) + brBox) * 0.125f);
        float3 param_25 = result;
        result = ApplyThreshold(param_25);
    }
    else
    {
        float3 param_26 = tl;
        float3 param_27 = tr;
        float3 param_28 = bl;
        float3 param_29 = br;
        bool param_30 = false;
        float3 centerBox_1 = EngineBloomAverage(param_26, param_27, param_28, param_29, param_30);
        float3 param_31 = tlf;
        float3 param_32 = t;
        float3 param_33 = l;
        float3 param_34 = center;
        bool param_35 = false;
        float3 tlBox_1 = EngineBloomAverage(param_31, param_32, param_33, param_34, param_35);
        float3 param_36 = trf;
        float3 param_37 = t;
        float3 param_38 = r;
        float3 param_39 = center;
        bool param_40 = false;
        float3 trBox_1 = EngineBloomAverage(param_36, param_37, param_38, param_39, param_40);
        float3 param_41 = blf;
        float3 param_42 = b;
        float3 param_43 = l;
        float3 param_44 = center;
        bool param_45 = false;
        float3 blBox_1 = EngineBloomAverage(param_41, param_42, param_43, param_44, param_45);
        float3 param_46 = brf;
        float3 param_47 = b;
        float3 param_48 = r;
        float3 param_49 = center;
        bool param_50 = false;
        float3 brBox_1 = EngineBloomAverage(param_46, param_47, param_48, param_49, param_50);
        result = (centerBox_1 * 0.5f) + ((((tlBox_1 + trBox_1) + blBox_1) + brBox_1) * 0.125f);
    }
    FragColor = float4(result, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vUv = stage_input.vUv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
