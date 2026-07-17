#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer BloomConstants
{
    float4 bloom_Parameters : packoffset(c0);
};

Texture2D<float4> sourceImage : register(t0, space0);
SamplerState _sourceImage_sampler : register(s0, space0);
RWTexture2D<float4> targetImage : register(u1, space0);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

uint2 spvTextureSize(Texture2D<float4> Tex, uint Level, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(Level, ret.x, ret.y, Param);
    return ret;
}

uint2 spvImageSize(RWTexture2D<float4> Tex, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(ret.x, ret.y);
    Param = 0u;
    return ret;
}

float3 Tap(float2 uv, int2 offset)
{
    uint _118_dummy_parameter;
    return sourceImage.SampleLevel(_sourceImage_sampler, uv + (float2(offset) / float2(int2(spvTextureSize(sourceImage, uint(0), _118_dummy_parameter)))), 0.0f).xyz;
}

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

void comp_main()
{
    int2 pixel = int2(gl_GlobalInvocationID.xy);
    uint _142_dummy_parameter;
    int2 size = int2(spvImageSize(targetImage, _142_dummy_parameter));
    if (any(bool2(pixel.x >= size.x, pixel.y >= size.y)))
    {
        return;
    }
    float2 uv = (float2(pixel) + 0.5f.xx) / float2(size);
    float2 param = uv;
    int2 param_1 = int2(0, 0);
    float3 center = Tap(param, param_1);
    float2 param_2 = uv;
    int2 param_3 = int2(-1, -1);
    float3 tl = Tap(param_2, param_3);
    float2 param_4 = uv;
    int2 param_5 = int2(1, -1);
    float3 tr = Tap(param_4, param_5);
    float2 param_6 = uv;
    int2 param_7 = int2(-1, 1);
    float3 bl = Tap(param_6, param_7);
    float2 param_8 = uv;
    int2 param_9 = int2(1, 1);
    float3 br = Tap(param_8, param_9);
    float2 param_10 = uv;
    int2 param_11 = int2(0, -2);
    float3 t = Tap(param_10, param_11);
    float2 param_12 = uv;
    int2 param_13 = int2(-2, 0);
    float3 l = Tap(param_12, param_13);
    float2 param_14 = uv;
    int2 param_15 = int2(2, 0);
    float3 r = Tap(param_14, param_15);
    float2 param_16 = uv;
    int2 param_17 = int2(0, 2);
    float3 b = Tap(param_16, param_17);
    float2 param_18 = uv;
    int2 param_19 = int2(-2, -2);
    float3 tlf = Tap(param_18, param_19);
    float2 param_20 = uv;
    int2 param_21 = int2(2, -2);
    float3 trf = Tap(param_20, param_21);
    float2 param_22 = uv;
    int2 param_23 = int2(-2, 2);
    float3 blf = Tap(param_22, param_23);
    float2 param_24 = uv;
    int2 param_25 = int2(2, 2);
    float3 brf = Tap(param_24, param_25);
    bool firstPass = bloom_Parameters.x > 0.5f;
    float3 param_26 = tl;
    float3 param_27 = tr;
    float3 param_28 = bl;
    float3 param_29 = br;
    bool param_30 = firstPass;
    float3 param_31 = tlf;
    float3 param_32 = t;
    float3 param_33 = l;
    float3 param_34 = center;
    bool param_35 = firstPass;
    float3 param_36 = trf;
    float3 param_37 = t;
    float3 param_38 = r;
    float3 param_39 = center;
    bool param_40 = firstPass;
    float3 param_41 = blf;
    float3 param_42 = b;
    float3 param_43 = l;
    float3 param_44 = center;
    bool param_45 = firstPass;
    float3 param_46 = brf;
    float3 param_47 = b;
    float3 param_48 = r;
    float3 param_49 = center;
    bool param_50 = firstPass;
    float3 result = (EngineBloomAverage(param_26, param_27, param_28, param_29, param_30) * 0.5f) + ((((EngineBloomAverage(param_31, param_32, param_33, param_34, param_35) + EngineBloomAverage(param_36, param_37, param_38, param_39, param_40)) + EngineBloomAverage(param_41, param_42, param_43, param_44, param_45)) + EngineBloomAverage(param_46, param_47, param_48, param_49, param_50)) * 0.125f);
    if (firstPass)
    {
        float3 param_51 = result;
        float luminance = EngineBloomLuminance(param_51) * bloom_Parameters.z;
        result *= clamp((luminance - bloom_Parameters.y) / max(luminance, 9.9999997473787516355514526367188e-05f), 0.0f, 1.0f);
    }
    targetImage[pixel] = float4(result, 1.0f);
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
