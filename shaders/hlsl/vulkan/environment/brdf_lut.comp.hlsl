RWTexture2D<float2> outputLut : register(u0, space0);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

uint2 spvImageSize(RWTexture2D<float2> Tex, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(ret.x, ret.y);
    Param = 0u;
    return ret;
}

float RadicalInverseVdc(inout uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 1431655765u) << 1u) | ((bits & 2863311530u) >> 1u);
    bits = ((bits & 858993459u) << 2u) | ((bits & 3435973836u) >> 2u);
    bits = ((bits & 252645135u) << 4u) | ((bits & 4042322160u) >> 4u);
    bits = ((bits & 16711935u) << 8u) | ((bits & 4278255360u) >> 8u);
    return float(bits) * 2.3283064365386962890625e-10f;
}

float2 Hammersley(uint i, uint count)
{
    uint param = i;
    float _93 = RadicalInverseVdc(param);
    return float2(float(i) / float(count), _93);
}

float3 ImportanceSampleGgx(float2 xi, float3 normal, float roughness)
{
    float a = roughness * roughness;
    float phi = 6.283185482025146484375f * xi.x;
    float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (((a * a) - 1.0f) * xi.y)));
    float sinTheta = sqrt(max(1.0f - (cosTheta * cosTheta), 0.0f));
    float3 halfVector = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    float3 up = (abs(normal.z) < 0.999000012874603271484375f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(((tangent * halfVector.x) + (bitangent * halfVector.y)) + (normal * halfVector.z));
}

float GeometrySchlickGgxIbl(float nDotV, float roughness)
{
    float k = (roughness * roughness) / 2.0f;
    return nDotV / ((nDotV * (1.0f - k)) + k);
}

void comp_main()
{
    int2 coordinate = int2(gl_GlobalInvocationID.xy);
    uint _209_dummy_parameter;
    int2 size = int2(spvImageSize(outputLut, _209_dummy_parameter));
    bool _215 = coordinate.x >= size.x;
    bool _224;
    if (!_215)
    {
        _224 = coordinate.y >= size.y;
    }
    else
    {
        _224 = _215;
    }
    if (_224)
    {
        return;
    }
    float2 uv = (float2(coordinate) + 0.5f.xx) / float2(size);
    float nDotV = max(uv.x, 0.001000000047497451305389404296875f);
    float roughness = uv.y;
    float3 viewDirection = float3(sqrt(1.0f - (nDotV * nDotV)), 0.0f, nDotV);
    float3 normal = float3(0.0f, 0.0f, 1.0f);
    float a = 0.0f;
    float b = 0.0f;
    for (uint i = 0u; i < 1024u; i++)
    {
        uint param = i;
        uint param_1 = 1024u;
        float2 param_2 = Hammersley(param, param_1);
        float3 param_3 = normal;
        float param_4 = roughness;
        float3 halfVector = ImportanceSampleGgx(param_2, param_3, param_4);
        float3 lightDirection = normalize((halfVector * (2.0f * dot(viewDirection, halfVector))) - viewDirection);
        float nDotL = max(lightDirection.z, 0.0f);
        float nDotH = max(halfVector.z, 0.0f);
        float vDotH = max(dot(viewDirection, halfVector), 0.0f);
        if (nDotL > 0.0f)
        {
            float param_5 = nDotV;
            float param_6 = roughness;
            float param_7 = nDotL;
            float param_8 = roughness;
            float geometry = GeometrySchlickGgxIbl(param_5, param_6) * GeometrySchlickGgxIbl(param_7, param_8);
            float visibility = (geometry * vDotH) / max(nDotH * nDotV, 9.9999999747524270787835121154785e-07f);
            float fresnel = pow(1.0f - vDotH, 5.0f);
            a += ((1.0f - fresnel) * visibility);
            b += (fresnel * visibility);
        }
    }
    outputLut[coordinate] = float4(float2(a, b) / 1024.0f.xx, 0.0f, 1.0f).xy;
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
