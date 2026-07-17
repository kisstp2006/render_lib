Texture2D<float4> uSceneColor : register(t0);
SamplerState _uSceneColor_sampler : register(s0);
cbuffer EngineGlobals_post_post_frag_hlsl : register(b13)
{
    bool uPostEnabled;
    float uExposure;
    float uBloomStrength;
    float uShoulderStrength;
    float uLinearStrength;
    float uLinearAngle;
    float uToeStrength;
    float uToeNumerator;
    float uToeDenominator;
    float uWhitePointScale;
    float uSaturation;
    float uContrast;
    float3 uColorTint;
    float uColorLutWeight;
    float3 uColorLutDomainMin;
    float3 uColorLutDomainMax;
    float uColorLutSize;
    bool uDitherEnabled;
};

Texture2D<float4> uBloom : register(t1);
SamplerState _uBloom_sampler : register(s1);
Texture3D<float4> uColorLut : register(t2);
SamplerState _uColorLut_sampler : register(s2);

static float4 gl_FragCoord;
static float2 vUv;
static float4 FragColor;

struct SPIRV_Cross_Input
{
#ifdef ENGINE_OPENGL
    [[vk::location(1)]]
#endif
    float2 vUv : TEXCOORD1;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float3 EngineUnchartedTonemap(float3 color, float4 curve0, float3 curve1)
{
    float3 numerator = (color * ((color * curve0.x) + (curve0.y * curve0.z).xxx)) + (curve1.x * curve0.w).xxx;
    float3 denominator = (color * ((color * curve0.x) + curve0.y.xxx)) + (curve1.y * curve0.w).xxx;
    return ((numerator / denominator) - (curve1.x / curve1.y).xxx) * curve1.z;
}

float3 EngineLinearToSrgb(float3 color)
{
    float3 low = color * 12.9200000762939453125f;
    float3 high = (pow(max(color, 0.0f.xxx), 0.4166666567325592041015625f.xxx) * 1.05499994754791259765625f) - 0.054999999701976776123046875f.xxx;
    return lerp(low, high, step(0.003130800090730190277099609375f.xxx, color));
}

float EngineHash12(float2 position)
{
    float3 p3 = frac(position.xyx * 0.103100001811981201171875f);
    p3 += dot(p3, p3.yzx + 33.3300018310546875f.xxx).xxx;
    return frac((p3.x + p3.y) * p3.z);
}

void frag_main()
{
    float3 color = uSceneColor.SampleLevel(_uSceneColor_sampler, vUv, 0.0f).xyz;
    if (uPostEnabled)
    {
        color *= uExposure;
        color += (uBloom.SampleLevel(_uBloom_sampler, vUv, 0.0f).xyz * uBloomStrength);
        float3 param = color;
        float4 param_1 = float4(uShoulderStrength, uLinearStrength, uLinearAngle, uToeStrength);
        float3 param_2 = float3(uToeNumerator, uToeDenominator, uWhitePointScale);
        color = EngineUnchartedTonemap(param, param_1, param_2);
        float3 param_3 = clamp(color, 0.0f.xxx, 1.0f.xxx);
        color = EngineLinearToSrgb(param_3);
        float luma = dot(color, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
        color = lerp(luma.xxx, color, uSaturation.xxx);
        color = ((color - 0.5f.xxx) * uContrast) + 0.5f.xxx;
        color = clamp(color * uColorTint, 0.0f.xxx, 1.0f.xxx);
        if (uColorLutWeight > 0.0f)
        {
            float3 domainColor = clamp((color - uColorLutDomainMin) / max(uColorLutDomainMax - uColorLutDomainMin, 9.9999999747524270787835121154785e-07f.xxx), 0.0f.xxx, 1.0f.xxx);
            float3 lutUv = (domainColor * ((uColorLutSize - 1.0f) / uColorLutSize)) + (0.5f / uColorLutSize).xxx;
            float3 graded = uColorLut.SampleLevel(_uColorLut_sampler, lutUv, 0.0f).xyz;
            color = lerp(color, graded, clamp(uColorLutWeight, 0.0f, 1.0f).xxx);
        }
        if (uDitherEnabled)
        {
            float2 param_4 = gl_FragCoord.xy;
            color += ((EngineHash12(param_4) - 0.5f) / 255.0f).xxx;
        }
    }
    else
    {
        float3 param_5 = clamp(color, 0.0f.xxx, 1.0f.xxx);
        color = EngineLinearToSrgb(param_5);
    }
    FragColor = float4(color, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    vUv = stage_input.vUv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
