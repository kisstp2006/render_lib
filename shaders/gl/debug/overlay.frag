#version 460 core

in vec2 vUv;
out vec4 FragColor;

uniform samplerBuffer uOverlay;
uniform vec4 uUvRect;
uniform vec2 uAtlasSize;

vec3 SrgbToLinear(vec3 value)
{
    const vec3 cutoff = vec3(0.04045);
    const vec3 low = value / 12.92;
    const vec3 high = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(value, cutoff));
}

void main()
{
    // CPU rows use top-left origin; OpenGL texture coordinates use bottom-left.
    vec2 localUv = vec2(vUv.x, 1.0 - vUv.y);
    vec2 atlasUv = uUvRect.xy + localUv * uUvRect.zw;
    ivec2 atlasPixel = clamp(ivec2(atlasUv * uAtlasSize), ivec2(0),
                             ivec2(uAtlasSize) - ivec2(1));
    int linearIndex = atlasPixel.y * int(uAtlasSize.x) + atlasPixel.x;
    vec4 encoded = texelFetch(uOverlay, linearIndex);
    // Buffer textures cannot use an sRGB internal format. Decode explicitly
    // so GL_FRAMEBUFFER_SRGB produces the same colors as the former sRGB atlas.
    FragColor = vec4(SrgbToLinear(encoded.rgb), encoded.a);
}
