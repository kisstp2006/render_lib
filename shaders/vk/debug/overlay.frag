#version 460

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D overlayTexture;
layout(push_constant) uniform OverlayConstants
{
    vec4 uvRect;
} overlay;

void main()
{
    outColor = texture(overlayTexture, overlay.uvRect.xy + uv * overlay.uvRect.zw);
}
