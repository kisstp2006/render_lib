#version 460
#include "../../common/color_pipeline.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform PostUniforms
{
    vec4 ExposureBloomPostLut;
    vec4 Curve0;
    vec4 Curve1;
    vec4 Grade;
    vec4 LutDomainMinSize;
    vec4 LutDomainMaxDither;
    vec4 Fxaa;
} post;
layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 2) uniform sampler2D bloomColor;
layout(set = 0, binding = 3) uniform sampler3D colorLut;

layout(push_constant) uniform OutputConstants
{
    int SrgbAttachment;
} outputTarget;

void main()
{
    vec3 color = textureLod(sceneColor, uv, 0.0).rgb;
    if (post.ExposureBloomPostLut.z > 0.5)
    {
        color *= post.ExposureBloomPostLut.x;
        color += textureLod(bloomColor, uv, 0.0).rgb * post.ExposureBloomPostLut.y;
        color = EngineLinearToSrgb(clamp(
            EngineUnchartedTonemap(color, post.Curve0, post.Curve1.xyz), 0.0, 1.0));

        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(vec3(luminance), color, post.Curve1.w);
        color = (color - 0.5) * post.Grade.x + 0.5;
        color = clamp(color * post.Grade.yzw, 0.0, 1.0);

        if (post.ExposureBloomPostLut.w > 0.0)
        {
            vec3 domainColor = clamp((color - post.LutDomainMinSize.xyz)
                / max(post.LutDomainMaxDither.xyz - post.LutDomainMinSize.xyz, vec3(1e-6)),
                0.0, 1.0);
            float lutSize = post.LutDomainMinSize.w;
            vec3 lutUv = domainColor * ((lutSize - 1.0) / lutSize) + 0.5 / lutSize;
            color = mix(color, textureLod(colorLut, lutUv, 0.0).rgb,
                        clamp(post.ExposureBloomPostLut.w, 0.0, 1.0));
        }
        if (post.LutDomainMaxDither.w > 0.5)
            color += (EngineHash12(gl_FragCoord.xy) - 0.5) / 255.0;
    }
    else
        color = EngineLinearToSrgb(clamp(color, 0.0, 1.0));

    // The selected swapchain uses an sRGB image, so its fixed-function write
    // performs the final encoding. The intermediate FXAA target is UNORM.
    if (outputTarget.SrgbAttachment != 0)
        color = EngineSrgbToLinear(clamp(color, 0.0, 1.0));
    outColor = vec4(color, 1.0);
}
