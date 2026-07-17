cbuffer MaterialUniforms : register(b0, space1)
{
    float4 material_BaseColorFactor : packoffset(c0);
    float4 material_EmissiveMetallic : packoffset(c1);
    float4 material_RoughnessAoAlphaCutoff : packoffset(c2);
    uint4 material_TextureFlags : packoffset(c3);
};

cbuffer ShadowUniforms : register(b0, space0)
{
    row_major float4x4 shadow_LightViewProjection : packoffset(c0);
    float4 shadow_LightPositionRange : packoffset(c4);
};

Texture2D<float4> baseColorMap : register(t1, space1);
SamplerState _baseColorMap_sampler : register(s1, space1);

static float gl_FragDepth;
static float2 uv;
static float3 worldPosition;

struct SPIRV_Cross_Input
{
    float3 worldPosition : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float gl_FragDepth : SV_Depth;
};

void frag_main()
{
    uint flags = material_TextureFlags.x;
    float alpha = material_BaseColorFactor.w;
    if ((flags & 1u) != 0u)
    {
        alpha *= baseColorMap.Sample(_baseColorMap_sampler, uv).w;
    }
    bool _51 = (flags & 32u) != 0u;
    bool _60;
    if (_51)
    {
        _60 = alpha < material_RoughnessAoAlphaCutoff.z;
    }
    else
    {
        _60 = _51;
    }
    if (_60)
    {
        discard;
    }
    gl_FragDepth = length(worldPosition - shadow_LightPositionRange.xyz) / max(shadow_LightPositionRange.w, 9.9999997473787516355514526367188e-05f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    uv = stage_input.uv;
    worldPosition = stage_input.worldPosition;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_FragDepth = gl_FragDepth;
    return stage_output;
}
