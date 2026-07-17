cbuffer EngineGlobals_debug_bounds_frag_hlsl : register(b13)
{
    float3 uColor;
};


static float4 oColor;

struct SPIRV_Cross_Output
{
    float4 oColor : SV_Target0;
};

void frag_main()
{
    oColor = float4(uColor, 1.0f);
}

SPIRV_Cross_Output main()
{
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.oColor = oColor;
    return stage_output;
}
