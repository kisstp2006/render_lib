static const float3 _63[24] = { 0.0f.xxx, float3(1.0f, 0.0f, 0.0f), float3(1.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 0.0f), float3(1.0f, 1.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), 0.0f.xxx, float3(0.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 1.0f), 1.0f.xxx, 1.0f.xxx, float3(0.0f, 1.0f, 1.0f), float3(0.0f, 1.0f, 1.0f), float3(0.0f, 0.0f, 1.0f), 0.0f.xxx, float3(0.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), float3(1.0f, 0.0f, 1.0f), float3(1.0f, 1.0f, 0.0f), 1.0f.xxx, float3(0.0f, 1.0f, 0.0f), float3(0.0f, 1.0f, 1.0f) };

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

#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer BoundsDebugConstants
{
    float4 boundsData_Minimum : packoffset(c0);
    float4 boundsData_Maximum : packoffset(c1);
    float4 boundsData_Color : packoffset(c2);
};


static float4 gl_Position;
static int gl_VertexIndex;
struct SPIRV_Cross_Input
{
    uint gl_VertexIndex : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float3 _24;
    if (boundsData_Color.w < 0.0f)
    {
        float3 _32;
        if (gl_VertexIndex == 0)
        {
            _32 = boundsData_Minimum.xyz;
        }
        else
        {
            _32 = boundsData_Maximum.xyz;
        }
        _24 = _32;
    }
    else
    {
        _24 = lerp(boundsData_Minimum.xyz, boundsData_Maximum.xyz, _63[gl_VertexIndex]);
    }
    float3 worldPosition = _24;
    gl_Position = mul(float4(worldPosition, 1.0f), mul(frame_View, frame_Projection));
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexIndex = int(stage_input.gl_VertexIndex);
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    return stage_output;
}
