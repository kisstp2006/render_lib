// One invocation produces one Hi-Z texel.  This intentionally avoids the
// dynamic nested reduction loops emitted by the earlier GLSL-to-HLSL rewrite:
// on NVIDIA's OpenGL SPIR-V driver those loops serialised texture/image
// hazards between mip dispatches and made a 1600x900 pyramid take >200 ms.
//
// A native mip level is normally half-size, but odd dimensions can make a
// destination texel cover up to 3x3 source texels.  The fixed, unrolled 3x3
// reduction below covers that case while retaining a conservative maximum.

RWTexture2D<float> uTarget : register(u1);

cbuffer EngineGlobals_visibility_hiz_build_comp_hlsl : register(b11)
{
    bool uCopySource;
    int uSourceMip;
};

Texture2D<float4> uSource : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint targetWidth, targetHeight;
    uTarget.GetDimensions(targetWidth, targetHeight);
    if (dispatchThreadId.x >= targetWidth || dispatchThreadId.y >= targetHeight)
        return;

    const int2 targetPixel = int2(dispatchThreadId.xy);
    if (uCopySource)
    {
        uTarget[targetPixel] = uSource.Load(int3(targetPixel, 0)).x;
        return;
    }

    uint sourceWidth, sourceHeight, sourceMipCount;
    uSource.GetDimensions(uSourceMip, sourceWidth, sourceHeight, sourceMipCount);
    const uint2 sourceSize = uint2(sourceWidth, sourceHeight);
    const uint2 targetSize = uint2(targetWidth, targetHeight);
    const uint2 sourceBegin = (dispatchThreadId.xy * sourceSize) / targetSize;
    const uint2 sourceEnd = ((dispatchThreadId.xy + 1u) * sourceSize) / targetSize;
    const uint2 sourceLast = max(sourceEnd, sourceBegin + 1u) - 1u;

    float farthest = 0.0f;
    [unroll]
    for (uint y = 0u; y < 3u; ++y)
    {
        const uint sampleY = min(sourceBegin.y + y, sourceLast.y);
        [unroll]
        for (uint x = 0u; x < 3u; ++x)
        {
            const uint sampleX = min(sourceBegin.x + x, sourceLast.x);
            farthest = max(farthest,
                            uSource.Load(int3(uint2(sampleX, sampleY), uSourceMip)).x);
        }
    }
    uTarget[targetPixel] = farthest;
}
