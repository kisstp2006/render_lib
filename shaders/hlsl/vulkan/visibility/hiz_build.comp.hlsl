// Fixed-cost conservative 3x3 reduction. Native mip levels normally map a
// target texel to 2x2 source texels; odd dimensions can cover 3x3. Keeping
// this reduction statically bounded lets both Vulkan and OpenGL compilers
// unroll it and avoids the pathological dynamic-loop path on NVIDIA OpenGL.

#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer HiZBuildConstants
{
    int4 uPush_Parameters : packoffset(c0);
};

RWTexture2D<float> uTarget : register(u1, space0);
Texture2D<float4> uSource : register(t0, space0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint targetWidth, targetHeight;
    uTarget.GetDimensions(targetWidth, targetHeight);
    if (dispatchThreadId.x >= targetWidth || dispatchThreadId.y >= targetHeight)
        return;

    const int sourceMip = uPush_Parameters.x;
    const int2 targetPixel = int2(dispatchThreadId.xy);
    if (uPush_Parameters.y != 0)
    {
        uTarget[targetPixel] = uSource.Load(int3(targetPixel, sourceMip)).x;
        return;
    }

    uint sourceWidth, sourceHeight, sourceMipCount;
    uSource.GetDimensions(sourceMip, sourceWidth, sourceHeight, sourceMipCount);
    const uint2 sourceSize = uint2(sourceWidth, sourceHeight);
    const uint2 targetSize = uint2(targetWidth, targetHeight);
    const uint2 sourceBegin = (dispatchThreadId.xy * sourceSize) / targetSize;
    const uint2 sourceEnd = ((dispatchThreadId.xy + 1u) * sourceSize) / targetSize;
    const uint2 sourceLast = max(sourceEnd, sourceBegin + 1u) - 1u;

    float maximumDepth = 0.0f;
    [unroll]
    for (uint y = 0u; y < 3u; ++y)
    {
        const uint sampleY = min(sourceBegin.y + y, sourceLast.y);
        [unroll]
        for (uint x = 0u; x < 3u; ++x)
        {
            const uint sampleX = min(sourceBegin.x + x, sourceLast.x);
            maximumDepth = max(maximumDepth,
                uSource.Load(int3(uint2(sampleX, sampleY), sourceMip)).x);
        }
    }
    uTarget[targetPixel] = maximumDepth;
}
