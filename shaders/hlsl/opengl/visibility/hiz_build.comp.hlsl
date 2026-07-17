static const uint3 gl_WorkGroupSize = uint3(8u, 8u, 1u);

RWTexture2D<float> uTarget : register(u1);
cbuffer EngineGlobals_visibility_hiz_build_comp_hlsl : register(b11)
{
    bool uCopySource;
    int uSourceMip;
};

Texture2D<float4> uSource : register(t0);
SamplerState _uSource_sampler : register(s0);

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
    int2 outputPixel = int2(gl_GlobalInvocationID.xy);
    uint _24_dummy_parameter;
    int2 outputSize = int2(spvImageSize(uTarget, _24_dummy_parameter));
    if (any(bool2(outputPixel.x >= outputSize.x, outputPixel.y >= outputSize.y)))
    {
        return;
    }
    if (uCopySource)
    {
        uTarget[outputPixel] = uSource.Load(int3(outputPixel, 0)).x.x;
        return;
    }
    uint _61_dummy_parameter;
    int2 sourceSize = int2(spvTextureSize(uSource, uint(uSourceMip), _61_dummy_parameter));
    int2 sourceBegin = (outputPixel * sourceSize) / outputSize;
    int2 sourceEnd = ((outputPixel + int2(1, 1)) * sourceSize) / outputSize;
    float farthest = 0.0f;
    for (int y = sourceBegin.y; y < sourceEnd.y; y++)
    {
        for (int x = sourceBegin.x; x < sourceEnd.x; x++)
        {
            farthest = max(farthest, uSource.Load(int3(int2(x, y), uSourceMip)).x);
        }
    }
    uTarget[outputPixel] = farthest.x;
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
