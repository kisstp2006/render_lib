#ifdef ENGINE_VULKAN
[[vk::push_constant]]
#endif
cbuffer OverlayConstants
{
    float4 overlay_uvRect : packoffset(c0);
};

Texture2D<float4> overlayTexture : register(t0, space0);
SamplerState _overlayTexture_sampler : register(s0, space0);

static float4 outColor;
static float2 uv;

struct SPIRV_Cross_Input
{
    float2 uv : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 outColor : SV_Target0;
};

void frag_main()
{
    outColor = overlayTexture.Sample(_overlayTexture_sampler, overlay_uvRect.xy + (uv * overlay_uvRect.zw));
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    uv = stage_input.uv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.outColor = outColor;
    return stage_output;
}
