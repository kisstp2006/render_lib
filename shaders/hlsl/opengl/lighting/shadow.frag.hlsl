cbuffer EngineGlobals_lighting_shadow_frag_hlsl : register(b13)
{
    float uBaseColorAlpha;
    bool uHasAlbedoMap;
    bool uAlphaMasked;
    float uAlphaCutoff;
};

Texture2D<float4> uAlbedoMap : register(t0);
SamplerState _uAlbedoMap_sampler : register(s0);

static float2 vUV;

struct SPIRV_Cross_Input
{
    float2 vUV : TEXCOORD0;
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
}

void main(SPIRV_Cross_Input stage_input)
{
    vUV = stage_input.vUV;
    frag_main();
}
