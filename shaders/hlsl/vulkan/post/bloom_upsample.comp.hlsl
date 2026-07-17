RWTexture2D<float4> targetImage : register(u1, space0);
Texture2D<float4> sourceImage : register(t0, space0);
SamplerState _sourceImage_sampler : register(s0, space0);

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

void comp_main()
{
    int2 pixel = int2(gl_GlobalInvocationID.xy);
    uint _24_dummy_parameter;
    int2 size = int2(spvImageSize(targetImage, _24_dummy_parameter));
    if (any(bool2(pixel.x >= size.x, pixel.y >= size.y)))
    {
        return;
    }
    float2 uv = (float2(pixel) + 0.5f.xx) / float2(size);
    uint _54_dummy_parameter;
    float2 texel = 1.0f.xx / float2(int2(spvTextureSize(sourceImage, uint(0), _54_dummy_parameter)));
    float3 color = 0.0f.xxx;
    color += (sourceImage.SampleLevel(_sourceImage_sampler, uv + (texel * (-1.0f).xx), 0.0f).xyz * 0.0625f);
    color += (sourceImage.SampleLevel(_sourceImage_sampler, uv + (texel * float2(0.0f, -1.0f)), 0.0f).xyz * 0.125f);
    color += (sourceImage.SampleLevel(_sourceImage_sampler, uv + (texel * float2(1.0f, -1.0f)), 0.0f).xyz * 0.0625f);
    color += (sourceImage.SampleLevel(_sourceImage_sampler, uv + (texel * float2(-1.0f, 0.0f)), 0.0f).xyz * 0.125f);
    color += (sourceImage.SampleLevel(_sourceImage_sampler, uv, 0.0f).xyz * 0.25f);
    color += (sourceImage.SampleLevel(_sourceImage_sampler, uv + (texel * float2(1.0f, 0.0f)), 0.0f).xyz * 0.125f);
    color += (sourceImage.SampleLevel(_sourceImage_sampler, uv + (texel * float2(-1.0f, 1.0f)), 0.0f).xyz * 0.0625f);
    color += (sourceImage.SampleLevel(_sourceImage_sampler, uv + (texel * float2(0.0f, 1.0f)), 0.0f).xyz * 0.125f);
    color += (sourceImage.SampleLevel(_sourceImage_sampler, uv + (texel * 1.0f.xx), 0.0f).xyz * 0.0625f);
    targetImage[pixel] = targetImage[pixel] + float4(color, 0.0f);
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
