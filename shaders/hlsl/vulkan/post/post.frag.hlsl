cbuffer PostUniforms : register(b0, space0)
{
    float4 post_ExposureBloomPostLut : packoffset(c0);
    float4 post_Curve0 : packoffset(c1);
    float4 post_Curve1 : packoffset(c2);
    float4 post_Grade : packoffset(c3);
    float4 post_LutDomainMinSize : packoffset(c4);
    float4 post_LutDomainMaxDither : packoffset(c5);
    float4 post_Fxaa : packoffset(c6);
};

#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer OutputConstants
{
    int outputTarget_SrgbAttachment : packoffset(c0);
};

Texture2D<float4> sceneColor : register(t1, space0);
SamplerState _sceneColor_sampler : register(s1, space0);
Texture2D<float4> bloomColor : register(t2, space0);
SamplerState _bloomColor_sampler : register(s2, space0);
Texture3D<float4> colorLut : register(t3, space0);
SamplerState _colorLut_sampler : register(s3, space0);

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

float3 EngineUnchartedTonemap(float3 color, float4 curve0, float3 curve1)
{
    float3 numerator = (color * ((color * curve0.x) + (curve0.y * curve0.z).xxx)) + (curve1.x * curve0.w).xxx;
    float3 denominator = (color * ((color * curve0.x) + curve0.y.xxx)) + (curve1.y * curve0.w).xxx;
    return ((numerator / denominator) - (curve1.x / curve1.y).xxx) * curve1.z;
}

float3 EngineLinearToSrgb(float3 color)
{
    float3 low = color * 12.9200000762939453125f;
    float3 high = (pow(max(color, 0.0f.xxx), 0.4166666567325592041015625f.xxx) * 1.05499994754791259765625f) - 0.054999999701976776123046875f.xxx;
    return lerp(low, high, step(0.003130800090730190277099609375f.xxx, color));
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
    float3 color = sceneColor.SampleLevel(_sceneColor_sampler, uv, 0.0f).xyz;
    if (post_ExposureBloomPostLut.z > 0.5f)
    {
        color *= post_ExposureBloomPostLut.x;
        color += (bloomColor.SampleLevel(_bloomColor_sampler, uv, 0.0f).xyz * post_ExposureBloomPostLut.y);
        float3 param = color;
        float4 param_1 = post_Curve0;
        float3 param_2 = post_Curve1.xyz;
        float3 param_3 = clamp(EngineUnchartedTonemap(param, param_1, param_2), 0.0f.xxx, 1.0f.xxx);
        color = EngineLinearToSrgb(param_3);
        float luminance = dot(color, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
        color = lerp(luminance.xxx, color, post_Curve1.w.xxx);
        color = ((color - 0.5f.xxx) * post_Grade.x) + 0.5f.xxx;
        color = clamp(color * post_Grade.yzw, 0.0f.xxx, 1.0f.xxx);
        if (post_ExposureBloomPostLut.w > 0.0f)
        {
            float3 domainColor = clamp((color - post_LutDomainMinSize.xyz) / max(post_LutDomainMaxDither.xyz - post_LutDomainMinSize.xyz, 9.9999999747524270787835121154785e-07f.xxx), 0.0f.xxx, 1.0f.xxx);
            float lutSize = post_LutDomainMinSize.w;
            float3 lutUv = (domainColor * ((lutSize - 1.0f) / lutSize)) + (0.5f / lutSize).xxx;
            color = lerp(color, colorLut.SampleLevel(_colorLut_sampler, lutUv, 0.0f).xyz, clamp(post_ExposureBloomPostLut.w, 0.0f, 1.0f).xxx);
        }
        if (post_LutDomainMaxDither.w > 0.5f)
        {
            float2 param_4 = gl_FragCoord.xy;
            color += ((EngineHash12(param_4) - 0.5f) / 255.0f).xxx;
        }
    }
    else
    {
        float3 param_5 = clamp(color, 0.0f.xxx, 1.0f.xxx);
        color = EngineLinearToSrgb(param_5);
    }
    if (outputTarget_SrgbAttachment != 0)
    {
        float3 param_6 = clamp(color, 0.0f.xxx, 1.0f.xxx);
        color = EngineSrgbToLinear(param_6);
    }
    outColor = float4(color, 1.0f);
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
