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

uint2 spvTextureSize(Texture2D<float4> Tex, uint Level, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(Level, ret.x, ret.y, Param);
    return ret;
}

void frag_main()
{
    uint _20_dummy_parameter;
    float2 texel = 1.0f.xx / float2(int2(spvTextureSize(uSource, uint(0), _20_dummy_parameter)));
    float dx = texel.x;
    float dy = texel.y;
    float3 c = 0.0f.xxx;
    c += (uSource.SampleLevel(_uSource_sampler, vUv + float2(-dx, -dy), 0.0f).xyz * 0.0625f);
    c += (uSource.SampleLevel(_uSource_sampler, vUv + float2(0.0f, -dy), 0.0f).xyz * 0.125f);
    c += (uSource.SampleLevel(_uSource_sampler, vUv + float2(dx, -dy), 0.0f).xyz * 0.0625f);
    c += (uSource.SampleLevel(_uSource_sampler, vUv + float2(-dx, 0.0f), 0.0f).xyz * 0.125f);
    c += (uSource.SampleLevel(_uSource_sampler, vUv, 0.0f).xyz * 0.25f);
    c += (uSource.SampleLevel(_uSource_sampler, vUv + float2(dx, 0.0f), 0.0f).xyz * 0.125f);
    c += (uSource.SampleLevel(_uSource_sampler, vUv + float2(-dx, dy), 0.0f).xyz * 0.0625f);
    c += (uSource.SampleLevel(_uSource_sampler, vUv + float2(0.0f, dy), 0.0f).xyz * 0.125f);
    c += (uSource.SampleLevel(_uSource_sampler, vUv + float2(dx, dy), 0.0f).xyz * 0.0625f);
    FragColor = float4(c, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vUv = stage_input.vUv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
