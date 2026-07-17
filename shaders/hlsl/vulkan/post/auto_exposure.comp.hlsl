RWByteAddressBuffer result : register(u1, space0);
Texture2D<float4> hdrImage : register(t0, space0);
SamplerState _hdrImage_sampler : register(s0, space0);

static uint3 gl_LocalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_LocalInvocationID : SV_GroupThreadID;
};

groupshared float partial[256];

void comp_main()
{
    uint threadIndex = gl_LocalInvocationID.x;
    float sum = 0.0f;
    for (uint sampleIndex = threadIndex; sampleIndex < 1024u; sampleIndex += 256u)
    {
        uint2 cell = uint2(sampleIndex & 31u, sampleIndex >> 5u);
        float2 uv = (float2(cell) + 0.5f.xx) / 32.0f.xx;
        float3 color = hdrImage.SampleLevel(_hdrImage_sampler, uv, 0.0f).xyz;
        sum += dot(color, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
    }
    partial[threadIndex] = sum;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 128u; stride > 0u; stride = stride >> 1u)
    {
        if (threadIndex < stride)
        {
            partial[threadIndex] += partial[threadIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (threadIndex == 0u)
    {
        result.Store(0, asuint(max(partial[0] / 1024.0f, 9.9999997473787516355514526367188e-05f)));
    }
}

[numthreads(256, 1, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_LocalInvocationID = stage_input.gl_LocalInvocationID;
    comp_main();
}
