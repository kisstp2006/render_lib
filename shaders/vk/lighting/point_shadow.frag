#version 460

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec2 uv;

layout(set = 0, binding = 0, std140) uniform ShadowUniforms
{
    mat4 LightViewProjection;
    vec4 LightPositionRange;
} shadow;

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
    float alpha = material.BaseColorFactor.a;
    if ((flags & HAS_BASE_COLOR_MAP) != 0u)
        alpha *= texture(baseColorMap, uv).a;
    if ((flags & USES_ALPHA_MASK) != 0u && alpha < material.RoughnessAoAlphaCutoff.z)
        discard;
    gl_FragDepth = length(worldPosition - shadow.LightPositionRange.xyz)
                 / max(shadow.LightPositionRange.w, 0.0001);
}
