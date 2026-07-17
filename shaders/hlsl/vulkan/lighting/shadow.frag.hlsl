cbuffer MaterialUniforms : register(b0, space1)
{
    float4 material_BaseColorFactor : packoffset(c0);
    float4 material_EmissiveMetallic : packoffset(c1);
    float4 material_RoughnessAoAlphaCutoff : packoffset(c2);
    uint4 material_TextureFlags : packoffset(c3);
};

Texture2D<float4> baseColorMap : register(t1, space1);
SamplerState _baseColorMap_sampler : register(s1, space1);

static float2 uv;

struct SPIRV_Cross_Input
{
    float2 uv : TEXCOORD0;
};

void frag_main()
{
    uint flags = material_TextureFlags.x;
    if ((flags & 32u) == 0u)
    {
        return;
    }
    float alpha = material_BaseColorFactor.w;
    if ((flags & 1u) != 0u)
    {
        alpha *= baseColorMap.Sample(_baseColorMap_sampler, uv).w;
    }
    if (alpha < material_RoughnessAoAlphaCutoff.z)
    {
        discard;
    }
}

void main(SPIRV_Cross_Input stage_input)
{
    uv = stage_input.uv;
    frag_main();
}
