struct EngineInstanceData
{
    row_major float4x4 Model;
    row_major float4x4 PreviousModel;
};

cbuffer ShadowUniforms : register(b0, space0)
{
    row_major float4x4 shadow_LightViewProjection : packoffset(c0);
};

ByteAddressBuffer _33 : register(t0, space2);

static float4 gl_Position;
static int gl_InstanceIndex;
static float2 uv;
static float2 inUv;
static float3 inPosition;

struct SPIRV_Cross_Input
{
#ifdef ENGINE_VULKAN
    [[vk::location(0)]]
#endif
    float3 inPosition : TEXCOORD0;
#ifdef ENGINE_VULKAN
    [[vk::location(3)]]
#endif
    float2 inUv : TEXCOORD3;
    uint gl_InstanceIndex : SV_InstanceID;
};

struct SPIRV_Cross_Output
{
    float2 uv : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    uv = inUv;
    float4x4 _40 = float4x4(
        asfloat(_33.Load4(uint(gl_InstanceIndex) * 128 + 0)),
        asfloat(_33.Load4(uint(gl_InstanceIndex) * 128 + 16)),
        asfloat(_33.Load4(uint(gl_InstanceIndex) * 128 + 32)),
        asfloat(_33.Load4(uint(gl_InstanceIndex) * 128 + 48)));
    gl_Position = mul(float4(inPosition, 1.0f), mul(_40, shadow_LightViewProjection));
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_InstanceIndex = int(stage_input.gl_InstanceIndex);
    inUv = stage_input.inUv;
    inPosition = stage_input.inPosition;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.uv = uv;
    return stage_output;
}
