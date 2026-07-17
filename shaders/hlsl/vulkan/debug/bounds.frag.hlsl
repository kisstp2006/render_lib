#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer BoundsDebugConstants
{
    float4 boundsData_Minimum : packoffset(c0);
    float4 boundsData_Maximum : packoffset(c1);
    float4 boundsData_Color : packoffset(c2);
};


static float4 outColor;
static float2 outVelocity;

struct SPIRV_Cross_Output
{
    float4 outColor : SV_Target0;
    float2 outVelocity : SV_Target1;
};

void frag_main()
{
    outColor = float4(boundsData_Color.xyz, 1.0f);
    outVelocity = 0.0f.xx;
}

SPIRV_Cross_Output main()
{
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.outColor = outColor;
    stage_output.outVelocity = outVelocity;
    return stage_output;
}
