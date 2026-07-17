cbuffer EngineGlobals_debug_bounds_vert_hlsl : register(b12)
{
    bool uLineMode;
    float3 uBoundsMinimum;
    float3 uBoundsMaximum;
    row_major float4x4 uViewProjection;
};


static float4 gl_Position;
static float3 aCorner;

struct SPIRV_Cross_Input
{
    float3 aCorner : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float3 _14;
    if (uLineMode)
    {
        _14 = lerp(uBoundsMinimum, uBoundsMaximum, aCorner.x.xxx);
    }
    else
    {
        _14 = lerp(uBoundsMinimum, uBoundsMaximum, aCorner);
    }
    float3 worldPosition = _14;
    gl_Position = mul(float4(worldPosition, 1.0f), uViewProjection);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aCorner = stage_input.aCorner;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    return stage_output;
}
