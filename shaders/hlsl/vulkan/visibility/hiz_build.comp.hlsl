#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer HiZBuildConstants
{
    int4 uPush_Parameters : packoffset(c0);
};

RWTexture2D<float> uTarget : register(u1, space0);
Texture2D<float4> uSource : register(t0, space0);
SamplerState _uSource_sampler : register(s0, space0);

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

uint2 spvImageSize(RWTexture2D<float> Tex, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(ret.x, ret.y);
    Param = 0u;
    return ret;
}

void comp_main()
{
    int2 targetPixel = int2(gl_GlobalInvocationID.xy);
    uint _24_dummy_parameter;
    int2 targetSize = int2(spvImageSize(uTarget, _24_dummy_parameter));
    if (any(bool2(targetPixel.x >= targetSize.x, targetPixel.y >= targetSize.y)))
    {
        return;
    }
    int sourceMip = uPush_Parameters.x;
    if (uPush_Parameters.y != 0)
    {
        uTarget[targetPixel] = uSource.Load(int3(targetPixel, sourceMip)).x.x;
        return;
    }
    uint _70_dummy_parameter;
    int2 sourceSize = int2(spvTextureSize(uSource, uint(sourceMip), _70_dummy_parameter));
    int2 sourceBegin = (targetPixel * sourceSize) / targetSize;
    int2 sourceEnd = ((targetPixel + int2(1, 1)) * sourceSize) / targetSize;
    float maximumDepth = 0.0f;
    for (int y = sourceBegin.y; y < sourceEnd.y; y++)
    {
        for (int x = sourceBegin.x; x < sourceEnd.x; x++)
        {
            maximumDepth = max(maximumDepth, uSource.Load(int3(int2(x, y), sourceMip)).x);
        }
    }
    uTarget[targetPixel] = maximumDepth.x;
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
