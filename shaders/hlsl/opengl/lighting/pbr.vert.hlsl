struct EngineInstanceData
{
    row_major float4x4 Model;
    row_major float4x4 PreviousModel;
};

ByteAddressBuffer _15 : register(t3);
cbuffer EngineGlobals_lighting_pbr_vert_hlsl : register(b12)
{
    row_major float4x4 uCurrentViewProjection;
    row_major float4x4 uPreviousViewProjection;
    uint uBaseInstance;
};


static float4 gl_Position;
static int gl_InstanceID;
static int gl_BaseInstanceARB;

static float3 aPosition;
static float3 aNormal;
static float4 aTangent;
static float3 vWorldPos;
static float3 vNormal;
static float4 vTangent;
static float2 vUV;
static float2 aUV;
static float4 vCurrentClip;
static float4 vPreviousClip;

struct SPIRV_Cross_Input
{
    float3 aPosition : TEXCOORD0;
    float3 aNormal : TEXCOORD1;
    float4 aTangent : TEXCOORD2;
    float2 aUV : TEXCOORD3;
    uint gl_InstanceID : SV_InstanceID;
};

struct SPIRV_Cross_Output
{
    float3 vWorldPos : TEXCOORD0;
    float3 vNormal : TEXCOORD1;
    float4 vTangent : TEXCOORD2;
    float2 vUV : TEXCOORD3;
    float4 vCurrentClip : TEXCOORD4;
    float4 vPreviousClip : TEXCOORD5;
    float4 gl_Position : SV_Position;
};

// Returns the determinant of a 2x2 matrix.
float spvDet2x2(float a1, float a2, float b1, float b2)
{
    return a1 * b2 - b1 * a2;
}

// Returns the inverse of a matrix, by using the algorithm of calculating the classical
// adjoint and dividing by the determinant. The contents of the matrix are changed.
float3x3 spvInverse(float3x3 m)
{
    float3x3 adj;	// The adjoint matrix (inverse after dividing by determinant)

    // Create the transpose of the cofactors, as the classical adjoint of the matrix.
    adj[0][0] =  spvDet2x2(m[1][1], m[1][2], m[2][1], m[2][2]);
    adj[0][1] = -spvDet2x2(m[0][1], m[0][2], m[2][1], m[2][2]);
    adj[0][2] =  spvDet2x2(m[0][1], m[0][2], m[1][1], m[1][2]);

    adj[1][0] = -spvDet2x2(m[1][0], m[1][2], m[2][0], m[2][2]);
    adj[1][1] =  spvDet2x2(m[0][0], m[0][2], m[2][0], m[2][2]);
    adj[1][2] = -spvDet2x2(m[0][0], m[0][2], m[1][0], m[1][2]);

    adj[2][0] =  spvDet2x2(m[1][0], m[1][1], m[2][0], m[2][1]);
    adj[2][1] = -spvDet2x2(m[0][0], m[0][1], m[2][0], m[2][1]);
    adj[2][2] =  spvDet2x2(m[0][0], m[0][1], m[1][0], m[1][1]);

    // Calculate the determinant as a combination of the cofactors of the first row.
    float det = (adj[0][0] * m[0][0]) + (adj[0][1] * m[1][0]) + (adj[0][2] * m[2][0]);

    // Divide the classical adjoint matrix by the determinant.
    // If determinant is zero, matrix is not invertable, so leave it unchanged.
    return (det != 0.0f) ? (adj * (1.0f / det)) : m;
}

void vert_main()
{
    uint instanceOffset = (uint(gl_BaseInstanceARB) + uint(gl_InstanceID)) * 128;
    float4x4 _29 = float4x4(
        asfloat(_15.Load4(instanceOffset + 0)),
        asfloat(_15.Load4(instanceOffset + 16)),
        asfloat(_15.Load4(instanceOffset + 32)),
        asfloat(_15.Load4(instanceOffset + 48)));
    float4x4 model = _29;
    float4x4 _38 = float4x4(
        asfloat(_15.Load4(instanceOffset + 64)),
        asfloat(_15.Load4(instanceOffset + 80)),
        asfloat(_15.Load4(instanceOffset + 96)),
        asfloat(_15.Load4(instanceOffset + 112)));
    float4x4 previousModel = _38;
    float4 worldPos = mul(float4(aPosition, 1.0f), model);
    float3 worldNormal = normalize(mul(aNormal, transpose(spvInverse(float3x3(model[0].xyz, model[1].xyz, model[2].xyz)))));
    float3 worldTangent = normalize(mul(aTangent.xyz, float3x3(model[0].xyz, model[1].xyz, model[2].xyz)));
    worldTangent = normalize(worldTangent - (worldNormal * dot(worldNormal, worldTangent)));
    vWorldPos = worldPos.xyz;
    vNormal = worldNormal;
    vTangent = float4(worldTangent, aTangent.w);
    vUV = aUV;
    vCurrentClip = mul(worldPos, uCurrentViewProjection);
    vPreviousClip = mul(float4(aPosition, 1.0f), mul(previousModel, uPreviousViewProjection));
    gl_Position = vCurrentClip;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_InstanceID = int(stage_input.gl_InstanceID);
    gl_BaseInstanceARB = int(uBaseInstance);
    aPosition = stage_input.aPosition;
    aNormal = stage_input.aNormal;
    aTangent = stage_input.aTangent;
    aUV = stage_input.aUV;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vWorldPos = vWorldPos;
    stage_output.vNormal = vNormal;
    stage_output.vTangent = vTangent;
    stage_output.vUV = vUV;
    stage_output.vCurrentClip = vCurrentClip;
    stage_output.vPreviousClip = vPreviousClip;
    return stage_output;
}
