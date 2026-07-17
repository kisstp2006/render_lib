cbuffer EngineGlobals_debug_overlay_frag_hlsl : register(b13)
{
    float4 uUvRect;
    float2 uAtlasSize;
};

Buffer<float4> uOverlay : register(t0);

static float2 vUv;
static float4 FragColor;

struct SPIRV_Cross_Input
{
#ifdef ENGINE_OPENGL
    [[vk::location(1)]]
#endif
    float2 vUv : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float3 SrgbToLinear(float3 value)
{
    float3 low = value / 12.9200000762939453125f.xxx;
    float3 high = pow((value + 0.054999999701976776123046875f.xxx) / 1.05499994754791259765625f.xxx, 2.400000095367431640625f.xxx);
    bool3 _36 = bool3(value.x <= 0.040449999272823333740234375f.xxx.x, value.y <= 0.040449999272823333740234375f.xxx.y, value.z <= 0.040449999272823333740234375f.xxx.z);
    return float3(_36.x ? low.x : high.x, _36.y ? low.y : high.y, _36.z ? low.z : high.z);
}

void frag_main()
{
    float2 localUv = float2(vUv.x, 1.0f - vUv.y);
    float2 atlasUv = uUvRect.xy + (localUv * uUvRect.zw);
    int2 atlasPixel = clamp(int2(atlasUv * uAtlasSize), int2(0, 0), int2(uAtlasSize) - int2(1, 1));
    int linearIndex = (atlasPixel.y * int(uAtlasSize.x)) + atlasPixel.x;
    float4 encoded = uOverlay.Load(linearIndex);
    float3 param = encoded.xyz;
    FragColor = float4(SrgbToLinear(param), encoded.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vUv = stage_input.vUv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
