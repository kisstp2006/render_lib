struct EngineInstanceData
{
    row_major float4x4 Model;
    row_major float4x4 PreviousModel;
};

ByteAddressBuffer _15 : register(t0, space2);
cbuffer ShadowUniforms : register(b0, space0)
{
    row_major float4x4 shadow_LightViewProjection : packoffset(c0);
    float4 shadow_LightPositionRange : packoffset(c4);
};


static float4 gl_Position;
static int gl_InstanceIndex;
static float3 inPosition;
static float3 worldPosition;
static float2 uv;
static float2 inUv;

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
    float3 worldPosition : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4x4 _25 = float4x4(
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 0)),
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 16)),
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 32)),
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 48)));
    float4 world = mul(float4(inPosition, 1.0f), _25);
    worldPosition = world.xyz;
    uv = inUv;
    gl_Position = mul(world, shadow_LightViewProjection);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_InstanceIndex = int(stage_input.gl_InstanceIndex);
    inPosition = stage_input.inPosition;
    inUv = stage_input.inUv;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.worldPosition = worldPosition;
    stage_output.uv = uv;
    return stage_output;
}
