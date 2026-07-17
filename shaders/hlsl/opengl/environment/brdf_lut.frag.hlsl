static float2 vUv;
static float2 FragColor;

struct SPIRV_Cross_Input
{
#ifdef ENGINE_OPENGL
    [[vk::location(1)]]
#endif
    float2 vUv : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float2 FragColor : SV_Target0;
};

float RadicalInverse_VdC(inout uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 1431655765u) << 1u) | ((bits & 2863311530u) >> 1u);
    bits = ((bits & 858993459u) << 2u) | ((bits & 3435973836u) >> 2u);
    bits = ((bits & 252645135u) << 4u) | ((bits & 4042322160u) >> 4u);
    bits = ((bits & 16711935u) << 8u) | ((bits & 4278255360u) >> 8u);
    return float(bits) * 2.3283064365386962890625e-10f;
}

float2 Hammersley(uint i, uint n)
{
    uint param = i;
    float _99 = RadicalInverse_VdC(param);
    return float2(float(i) / float(n), _99);
}

float3 ImportanceSampleGGX(float2 xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 6.283185482025146484375f * xi.x;
    float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (((a * a) - 1.0f) * xi.y)));
    float sinTheta = sqrt(1.0f - (cosTheta * cosTheta));
    float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    bool3 _156 = (abs(N.z) < 0.999000012874603271484375f).xxx;
    float3 up = float3(_156.x ? float3(0.0f, 0.0f, 1.0f).x : float3(1.0f, 0.0f, 0.0f).x, _156.y ? float3(0.0f, 0.0f, 1.0f).y : float3(1.0f, 0.0f, 0.0f).y, _156.z ? float3(0.0f, 0.0f, 1.0f).z : float3(1.0f, 0.0f, 0.0f).z);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(((tangent * H.x) + (bitangent * H.y)) + (N * H.z));
}

float GeometrySchlickGGX_IBL(float NoV, float roughness)
{
    float k = (roughness * roughness) / 2.0f;
    return NoV / ((NoV * (1.0f - k)) + k);
}

float GeometrySmith_IBL(float NoV, float NoL, float roughness)
{
    float param = NoV;
    float param_1 = roughness;
    float param_2 = NoL;
    float param_3 = roughness;
    return GeometrySchlickGGX_IBL(param, param_1) * GeometrySchlickGGX_IBL(param_2, param_3);
}

void frag_main()
{
    float NoV = max(vUv.x, 0.001000000047497451305389404296875f);
    float roughness = vUv.y;
    float3 V = float3(sqrt(1.0f - (NoV * NoV)), 0.0f, NoV);
    float3 N = float3(0.0f, 0.0f, 1.0f);
    float A = 0.0f;
    float B = 0.0f;
    for (uint i = 0u; i < 1024u; i++)
    {
        uint param = i;
        uint param_1 = 1024u;
        float2 xi = Hammersley(param, param_1);
        float2 param_2 = xi;
        float3 param_3 = N;
        float param_4 = roughness;
        float3 H = ImportanceSampleGGX(param_2, param_3, param_4);
        float3 L = normalize((H * (2.0f * dot(V, H))) - V);
        float NoL = max(L.z, 0.0f);
        float NoH = max(H.z, 0.0f);
        float VoH = max(dot(V, H), 0.0f);
        if (NoL > 0.0f)
        {
            float param_5 = NoV;
            float param_6 = NoL;
            float param_7 = roughness;
            float G = GeometrySmith_IBL(param_5, param_6, param_7);
            float GVis = (G * VoH) / (NoH * NoV);
            float Fc = pow(1.0f - VoH, 5.0f);
            A += ((1.0f - Fc) * GVis);
            B += (Fc * GVis);
        }
    }
    FragColor = float2(A, B) / 1024.0f.xx;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vUv = stage_input.vUv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
