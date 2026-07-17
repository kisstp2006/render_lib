struct EngineInstanceData
{
    row_major float4x4 Model;
    row_major float4x4 PreviousModel;
};

ByteAddressBuffer _30 : register(t3);
cbuffer EngineGlobals_lighting_shadow_vert_hlsl : register(b12)
{
    row_major float4x4 uLightSpaceMatrix;
    uint uBaseInstance;
};


static float4 gl_Position;
static int gl_InstanceID;
static int gl_BaseInstanceARB;

static float2 vUV;
static float2 aUV;
static float3 aPosition;

struct SPIRV_Cross_Input
{
    float3 aPosition : TEXCOORD0;
    float2 aUV : TEXCOORD3;
    uint gl_InstanceID : SV_InstanceID;
};

struct SPIRV_Cross_Output
{
    float2 vUV : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    vUV = aUV;
    uint instanceOffset = (uint(gl_BaseInstanceARB) + uint(gl_InstanceID)) * 128;
    float4x4 _41 = float4x4(
        asfloat(_30.Load4(instanceOffset + 0)),
        asfloat(_30.Load4(instanceOffset + 16)),
        asfloat(_30.Load4(instanceOffset + 32)),
        asfloat(_30.Load4(instanceOffset + 48)));
    gl_Position = mul(float4(aPosition, 1.0f), mul(_41, uLightSpaceMatrix));
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_InstanceID = int(stage_input.gl_InstanceID);
    gl_BaseInstanceARB = int(uBaseInstance);
    aUV = stage_input.aUV;
    aPosition = stage_input.aPosition;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vUV = vUV;
    return stage_output;
}
