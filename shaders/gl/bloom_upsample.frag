#version 460 core

// 9-tap tent-filter upsample (VRF's D_BLUR_PASS 2), rendered with additive
// blending into the next-larger bloom level.

in vec2 vUv;
out vec4 FragColor;

uniform sampler2D uSource;

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(uSource, 0));
    float dx = texel.x;
    float dy = texel.y;

    vec3 c = vec3(0.0);
    c += textureLod(uSource, vUv + vec2(-dx, -dy), 0.0).rgb * 0.0625;
    c += textureLod(uSource, vUv + vec2(0.0, -dy), 0.0).rgb * 0.125;
    c += textureLod(uSource, vUv + vec2(dx, -dy), 0.0).rgb * 0.0625;
    c += textureLod(uSource, vUv + vec2(-dx, 0.0), 0.0).rgb * 0.125;
    c += textureLod(uSource, vUv, 0.0).rgb * 0.25;
    c += textureLod(uSource, vUv + vec2(dx, 0.0), 0.0).rgb * 0.125;
    c += textureLod(uSource, vUv + vec2(-dx, dy), 0.0).rgb * 0.0625;
    c += textureLod(uSource, vUv + vec2(0.0, dy), 0.0).rgb * 0.125;
    c += textureLod(uSource, vUv + vec2(dx, dy), 0.0).rgb * 0.0625;

    FragColor = vec4(c, 1.0);
}
