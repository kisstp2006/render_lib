ByteAddressBuffer uCandidateStorage : register(t1, space0);
RWByteAddressBuffer uResults : register(u2, space0);
#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer OcclusionConstants
{
    row_major float4x4 uPush_ViewProjection : packoffset(c0);
    float4 uPush_Parameters : packoffset(c4);
};

Texture2D<float4> uHiZ : register(t0, space0);
SamplerState _uHiZ_sampler : register(s0, space0);

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

bool hizBoundsVisible(float4x4 viewProjection, float3 boundsMin, float3 boundsMax, Texture2D<float4> hiZ, SamplerState _hiZ_sampler, float depthBias, int maxMip)
{
    float2 uvMin = 1.0f.xx;
    float2 uvMax = 0.0f.xx;
    float objectNear = 1.0f;
    float _52;
    float _67;
    float _81;
    for (int corner = 0; corner < 8; corner++)
    {
        if ((corner & 1) != 0)
        {
            _52 = boundsMax.x;
        }
        else
        {
            _52 = boundsMin.x;
        }
        if ((corner & 2) != 0)
        {
            _67 = boundsMax.y;
        }
        else
        {
            _67 = boundsMin.y;
        }
        if ((corner & 4) != 0)
        {
            _81 = boundsMax.z;
        }
        else
        {
            _81 = boundsMin.z;
        }
        float3 position = float3(_52, _67, _81);
        float4 clip = mul(float4(position, 1.0f), viewProjection);
        if (clip.w <= 9.9999997473787516355514526367188e-06f)
        {
            return true;
        }
        float3 ndc = clip.xyz / clip.w.xxx;
        float2 uv = (ndc.xy * 0.5f) + 0.5f.xx;
        uvMin = min(uvMin, uv);
        uvMax = max(uvMax, uv);
        objectNear = min(objectNear, ndc.z);
    }
    bool _138 = uvMax.x <= 0.0f;
    bool _145;
    if (!_138)
    {
        _145 = uvMax.y <= 0.0f;
    }
    else
    {
        _145 = _138;
    }
    bool _152;
    if (!_145)
    {
        _152 = uvMin.x >= 1.0f;
    }
    else
    {
        _152 = _145;
    }
    bool _159;
    if (!_152)
    {
        _159 = uvMin.y >= 1.0f;
    }
    else
    {
        _159 = _152;
    }
    if (_159)
    {
        return true;
    }
    uvMin = clamp(uvMin, 0.0f.xx, 1.0f.xx);
    uvMax = clamp(uvMax, 0.0f.xx, 1.0f.xx);
    if (objectNear <= 0.0f)
    {
        return true;
    }
    uint _176_dummy_parameter;
    float2 fullSize = float2(int2(spvTextureSize(hiZ, uint(0), _176_dummy_parameter)));
    float pixelSpan = max((uvMax.x - uvMin.x) * fullSize.x, (uvMax.y - uvMin.y) * fullSize.y);
    int mip = clamp(int(ceil(log2(max(pixelSpan, 1.0f)))), 0, maxMip);
    uint _209_dummy_parameter;
    int2 mipSize = int2(spvTextureSize(hiZ, uint(mip), _209_dummy_parameter));
    int2 lo = clamp(int2(floor(uvMin * float2(mipSize))), int2(0, 0), mipSize - int2(1, 1));
    int2 hi = clamp(int2(floor(uvMax * float2(mipSize))), int2(0, 0), mipSize - int2(1, 1));
    int2 center = (lo + hi) / int2(2, 2);
    float sceneFar = hiZ.Load(int3(lo, mip)).x;
    sceneFar = max(sceneFar, hiZ.Load(int3(int2(hi.x, lo.y), mip)).x);
    sceneFar = max(sceneFar, hiZ.Load(int3(int2(lo.x, hi.y), mip)).x);
    sceneFar = max(sceneFar, hiZ.Load(int3(hi, mip)).x);
    sceneFar = max(sceneFar, hiZ.Load(int3(center, mip)).x);
    return objectNear <= (sceneFar + depthBias);
}

void comp_main()
{
    uint index = gl_GlobalInvocationID.x;
    uint candidateCount = uint(uPush_Parameters.z);
    if (index >= candidateCount)
    {
        return;
    }
    float3 boundsMin = asfloat(uCandidateStorage.Load4((index * 2u) * 16 + 0)).xyz;
    float3 boundsMax = asfloat(uCandidateStorage.Load4(((index * 2u) + 1u) * 16 + 0)).xyz;
    float4x4 param = uPush_ViewProjection;
    float3 param_1 = boundsMin;
    float3 param_2 = boundsMax;
    float param_3 = uPush_Parameters.x;
    int param_4 = int(uPush_Parameters.y);
    bool visible = hizBoundsVisible(param, param_1, param_2, uHiZ, _uHiZ_sampler, param_3, param_4);
    uResults.Store(index * 4 + 0, uint(visible));
}

[numthreads(64, 1, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
