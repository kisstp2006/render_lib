#version 460

layout(location = 0) in vec2 uv;

layout(set = 1, binding = 0, std140) uniform MaterialUniforms
{
    vec4 BaseColorFactor;
    vec4 EmissiveMetallic;
    vec4 RoughnessAoAlphaCutoff;
    uvec4 TextureFlags;
} material;

layout(set = 1, binding = 1) uniform sampler2D baseColorMap;

const uint HAS_BASE_COLOR_MAP = 1u << 0u;
const uint USES_ALPHA_MASK = 1u << 5u;

void main()
{
    uint flags = material.TextureFlags.x;
    if ((flags & USES_ALPHA_MASK) == 0u)
        return;
    float alpha = material.BaseColorFactor.a;
    if ((flags & HAS_BASE_COLOR_MAP) != 0u)
        alpha *= texture(baseColorMap, uv).a;
    if (alpha < material.RoughnessAoAlphaCutoff.z)
        discard;
}
