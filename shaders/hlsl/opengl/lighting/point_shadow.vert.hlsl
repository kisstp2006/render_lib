struct EngineInstanceData
{
    row_major float4x4 Model;
    row_major float4x4 PreviousModel;
};

ByteAddressBuffer _15 : register(t3);
cbuffer EngineGlobals_lighting_point_shadow_vert_hlsl : register(b12)
{
    row_major float4x4 uFaceMatrix;
    uint uBaseInstance;
};


static float4 gl_Position;
static int gl_InstanceID;
static int gl_BaseInstanceARB;

static float3 aPosition;
static float3 vWorldPos;
static float2 vUV;
static float2 aUV;

struct SPIRV_Cross_Input
{
    float3 aPosition : TEXCOORD0;
    float2 aUV : TEXCOORD3;
    uint gl_InstanceID : SV_InstanceID;
};

struct SPIRV_Cross_Output
{
    float3 vWorldPos : TEXCOORD0;
    float2 vUV : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    uint instanceOffset = (uint(gl_BaseInstanceARB) + uint(gl_InstanceID)) * 128;
    float4x4 _29 = float4x4(
        asfloat(_15.Load4(instanceOffset + 0)),
        asfloat(_15.Load4(instanceOffset + 16)),
        asfloat(_15.Load4(instanceOffset + 32)),
        asfloat(_15.Load4(instanceOffset + 48)));
    float4 world = mul(float4(aPosition, 1.0f), _29);
    vWorldPos = world.xyz;
    vUV = aUV;
    gl_Position = mul(world, uFaceMatrix);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_InstanceID = int(stage_input.gl_InstanceID);
    gl_BaseInstanceARB = int(uBaseInstance);
    aPosition = stage_input.aPosition;
    aUV = stage_input.aUV;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vWorldPos = vWorldPos;
    stage_output.vUV = vUV;
    return stage_output;
}
