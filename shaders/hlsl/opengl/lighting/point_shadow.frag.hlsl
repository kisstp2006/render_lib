cbuffer EngineGlobals_lighting_point_shadow_frag_hlsl : register(b13)
{
    float uBaseColorAlpha;
    bool uHasAlbedoMap;
    bool uAlphaMasked;
    float uAlphaCutoff;
    float3 uLightPosition;
    float uLightRange;
};

Texture2D<float4> uAlbedoMap : register(t0);
SamplerState _uAlbedoMap_sampler : register(s0);

static float gl_FragDepth;
static float2 vUV;
static float3 vWorldPos;

struct SPIRV_Cross_Input
{
    float3 vWorldPos : TEXCOORD0;
    float2 vUV : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float gl_FragDepth : SV_Depth;
};

void frag_main()
{
    float alpha = uBaseColorAlpha;
    if (uHasAlbedoMap)
    {
        alpha *= uAlbedoMap.Sample(_uAlbedoMap_sampler, vUV).w;
    }
    if (uAlphaMasked && (alpha < uAlphaCutoff))
    {
        discard;
    }
    gl_FragDepth = length(vWorldPos - uLightPosition) / uLightRange;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vUV = stage_input.vUV;
    vWorldPos = stage_input.vWorldPos;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_FragDepth = gl_FragDepth;
    return stage_output;
}
