struct EngineInstanceData
{
    row_major float4x4 Model;
    row_major float4x4 PreviousModel;
};

ByteAddressBuffer _15 : register(t0, space2);
cbuffer FrameUniforms : register(b0, space0)
{
    row_major float4x4 frame_View : packoffset(c0);
    row_major float4x4 frame_Projection : packoffset(c4);
    row_major float4x4 frame_PreviousViewProjection : packoffset(c8);
    float4 frame_CameraPosition : packoffset(c12);
    float4 frame_SunDirectionIntensity : packoffset(c13);
    float4 frame_SunColor : packoffset(c14);
    float4 frame_SkyZenithIntensity : packoffset(c15);
    float4 frame_SkyHorizonPostEnabled : packoffset(c16);
    float4 frame_SkyGroundExposure : packoffset(c17);
    float4 frame_SkySunParameters : packoffset(c18);
    float4 frame_PostCurve0 : packoffset(c19);
    float4 frame_PostCurve1 : packoffset(c20);
    float4 frame_PostGrade : packoffset(c21);
    float4 frame_FogColorOpacity : packoffset(c22);
    float4 frame_FogStartEndExponents : packoffset(c23);
    float4 frame_FogHeightEnabled : packoffset(c24);
    float4 frame_NightZenithIntensity : packoffset(c25);
    float4 frame_NightHorizonGlow : packoffset(c26);
    float4 frame_MilkyWayColorIntensity : packoffset(c27);
    float4 frame_StarWarmDensity : packoffset(c28);
    float4 frame_StarCoolSize : packoffset(c29);
    float4 frame_StarAnimation : packoffset(c30);
    float4 frame_NightRotationFlags : packoffset(c31);
    float4 frame_SkyFeatureFlags : packoffset(c32);
    row_major float4x4 frame_CascadeMatrices[4] : packoffset(c33);
    float4 frame_CascadeSplits : packoffset(c49);
    float4 frame_ShadowParameters : packoffset(c50);
    uint4 frame_LightCounts : packoffset(c51);
    float4 frame_PointPositionRadius[8] : packoffset(c52);
    float4 frame_PointColor[8] : packoffset(c60);
    float4 frame_SpotPositionRange[4] : packoffset(c68);
    float4 frame_SpotDirectionCosOuter[4] : packoffset(c72);
    float4 frame_SpotColorCosInner[4] : packoffset(c76);
    float4 frame_AreaPositionRange[4] : packoffset(c80);
    float4 frame_AreaDirectionMinRoughness[4] : packoffset(c84);
    float4 frame_AreaRightHalfWidth[4] : packoffset(c88);
    float4 frame_AreaUpHalfHeight[4] : packoffset(c92);
    float4 frame_AreaColor[4] : packoffset(c96);
    float4 frame_AreaSoftness[4] : packoffset(c100);
    float4 frame_PointShadowCookie[8] : packoffset(c104);
    row_major float4x4 frame_SpotMatrices[4] : packoffset(c112);
    float4 frame_SpotShadowRects[4] : packoffset(c128);
    float4 frame_SpotCookieData[4] : packoffset(c132);
    row_major float4x4 frame_AreaMatrices[4] : packoffset(c136);
    float4 frame_AreaShadowRects[4] : packoffset(c152);
    float4 frame_AreaCookieData[4] : packoffset(c156);
};


static float4 gl_Position;
static int gl_InstanceIndex;
static float3 inPosition;
static float3 inNormal;
static float4 inTangent;
static float3 worldPosition;
static float3 worldNormal;
static float4 worldTangent;
static float2 uv;
static float2 inUv;
static float4 currentClip;
static float4 previousClip;

struct SPIRV_Cross_Input
{
    float3 inPosition : TEXCOORD0;
    float3 inNormal : TEXCOORD1;
    float4 inTangent : TEXCOORD2;
    float2 inUv : TEXCOORD3;
    uint gl_InstanceIndex : SV_InstanceID;
};

struct SPIRV_Cross_Output
{
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float4 worldTangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float4 currentClip : TEXCOORD4;
    float4 previousClip : TEXCOORD5;
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
    float4x4 _25 = float4x4(
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 0)),
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 16)),
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 32)),
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 48)));
    float4x4 model = _25;
    float4x4 _31 = float4x4(
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 64)),
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 80)),
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 96)),
        asfloat(_15.Load4(uint(gl_InstanceIndex) * 128 + 112)));
    float4x4 previousModel = _31;
    float4 world = mul(float4(inPosition, 1.0f), model);
    float3x3 normalMatrix = transpose(spvInverse(float3x3(model[0].xyz, model[1].xyz, model[2].xyz)));
    float3 transformedNormal = normalize(mul(inNormal, normalMatrix));
    float3 transformedTangent = normalize(mul(inTangent.xyz, float3x3(model[0].xyz, model[1].xyz, model[2].xyz)));
    transformedTangent = normalize(transformedTangent - (transformedNormal * dot(transformedNormal, transformedTangent)));
    worldPosition = world.xyz;
    worldNormal = transformedNormal;
    worldTangent = float4(transformedTangent, inTangent.w);
    uv = inUv;
    currentClip = mul(world, mul(frame_View, frame_Projection));
    previousClip = mul(float4(inPosition, 1.0f), mul(previousModel, frame_PreviousViewProjection));
    gl_Position = currentClip;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_InstanceIndex = int(stage_input.gl_InstanceIndex);
    inPosition = stage_input.inPosition;
    inNormal = stage_input.inNormal;
    inTangent = stage_input.inTangent;
    inUv = stage_input.inUv;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.worldPosition = worldPosition;
    stage_output.worldNormal = worldNormal;
    stage_output.worldTangent = worldTangent;
    stage_output.uv = uv;
    stage_output.currentClip = currentClip;
    stage_output.previousClip = previousClip;
    return stage_output;
}
