cbuffer EngineGlobals_environment_equirect_to_cube_frag_hlsl : register(b13)
{
    row_major float3x3 uFaceBasis;
    float uRotation;
    float uIntensity;
};

Texture2D<float4> uEquirectangularMap : register(t0);
SamplerState _uEquirectangularMap_sampler : register(s0);

static float2 vNdc;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float2 vNdc : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

void frag_main()
{
    float3 direction = normalize(mul(float3(vNdc, 1.0f), uFaceBasis));
    float longitude = atan2(direction.z, direction.x) + uRotation;
    float2 uv = float2(frac((longitude / 6.283185482025146484375f) + 0.5f), acos(clamp(direction.y, -1.0f, 1.0f)) / 3.1415927410125732421875f);
    float3 radiance = max(uEquirectangularMap.Sample(_uEquirectangularMap_sampler, uv).xyz, 0.0f.xxx);
    FragColor = float4(radiance * uIntensity, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vNdc = stage_input.vNdc;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
