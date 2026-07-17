cbuffer EngineGlobals_environment_irradiance_frag_hlsl : register(b13)
{
    row_major float3x3 uFaceBasis;
    float uSampleDelta;
};

TextureCube<float4> uEnvMap : register(t0);
SamplerState _uEnvMap_sampler : register(s0);

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
    float3 N = normalize(mul(float3(vNdc, 1.0f), uFaceBasis));
    bool3 _38 = (abs(N.y) < 0.999000012874603271484375f).xxx;
    float3 up = float3(_38.x ? float3(0.0f, 1.0f, 0.0f).x : float3(1.0f, 0.0f, 0.0f).x, _38.y ? float3(0.0f, 1.0f, 0.0f).y : float3(1.0f, 0.0f, 0.0f).y, _38.z ? float3(0.0f, 1.0f, 0.0f).z : float3(1.0f, 0.0f, 0.0f).z);
    float3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));
    float3 irradiance = 0.0f.xxx;
    int sampleCount = 0;
    float delta = max(uSampleDelta, 0.02500000037252902984619140625f);
    for (float phi = 0.0f; phi < 6.283185482025146484375f; phi += delta)
    {
        for (float theta = 0.0f; theta < 1.57079637050628662109375f; theta += delta)
        {
            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 sampleDir = ((right * tangentSample.x) + (up * tangentSample.y)) + (N * tangentSample.z);
            irradiance += ((uEnvMap.SampleLevel(_uEnvMap_sampler, sampleDir, 3.0f).xyz * cos(theta)) * sin(theta));
            sampleCount++;
        }
    }
    irradiance = (irradiance * 3.1415927410125732421875f) / float(sampleCount).xxx;
    FragColor = float4(irradiance, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vNdc = stage_input.vNdc;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
