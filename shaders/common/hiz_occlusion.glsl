// Conservative AABB test against a maximum-depth Hi-Z pyramid.
// OCCLUSION_ZERO_TO_ONE must be 1 for Vulkan clip space and 0 for OpenGL.
bool hizBoundsVisible(mat4 viewProjection, vec3 boundsMin, vec3 boundsMax,
                      sampler2D hiZ, float depthBias, int maxMip)
{
    vec2 uvMin = vec2(1.0);
    vec2 uvMax = vec2(0.0);
    float objectNear = 1.0;
    for (int corner = 0; corner < 8; ++corner)
    {
        vec3 position = vec3(
            (corner & 1) != 0 ? boundsMax.x : boundsMin.x,
            (corner & 2) != 0 ? boundsMax.y : boundsMin.y,
            (corner & 4) != 0 ? boundsMax.z : boundsMin.z);
        vec4 clip = viewProjection * vec4(position, 1.0);
        if (clip.w <= 1.0e-5)
            return true;
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        uvMin = min(uvMin, uv);
        uvMax = max(uvMax, uv);
#if OCCLUSION_ZERO_TO_ONE
        objectNear = min(objectNear, ndc.z);
#else
        objectNear = min(objectNear, ndc.z * 0.5 + 0.5);
#endif
    }

    if (uvMax.x <= 0.0 || uvMax.y <= 0.0 || uvMin.x >= 1.0 || uvMin.y >= 1.0)
        return true;
    uvMin = clamp(uvMin, vec2(0.0), vec2(1.0));
    uvMax = clamp(uvMax, vec2(0.0), vec2(1.0));
    if (objectNear <= 0.0)
        return true;

    vec2 fullSize = vec2(textureSize(hiZ, 0));
    float pixelSpan = max((uvMax.x - uvMin.x) * fullSize.x,
                          (uvMax.y - uvMin.y) * fullSize.y);
    int mip = clamp(int(ceil(log2(max(pixelSpan, 1.0)))), 0, maxMip);
    ivec2 mipSize = textureSize(hiZ, mip);
    ivec2 lo = clamp(ivec2(floor(uvMin * vec2(mipSize))),
                         ivec2(0), mipSize - 1);
    ivec2 hi = clamp(ivec2(floor(uvMax * vec2(mipSize))),
                         ivec2(0), mipSize - 1);
    ivec2 center = (lo + hi) / 2;
    float sceneFar = texelFetch(hiZ, lo, mip).r;
    sceneFar = max(sceneFar, texelFetch(hiZ, ivec2(hi.x, lo.y), mip).r);
    sceneFar = max(sceneFar, texelFetch(hiZ, ivec2(lo.x, hi.y), mip).r);
    sceneFar = max(sceneFar, texelFetch(hiZ, hi, mip).r);
    sceneFar = max(sceneFar, texelFetch(hiZ, center, mip).r);
    return objectNear <= sceneFar + depthBias;
}
