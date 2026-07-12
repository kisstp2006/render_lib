#version 460 core

in vec2 vUV;

uniform bool uAlphaMasked;
uniform bool uHasAlbedoMap;
uniform float uBaseColorAlpha;
uniform float uAlphaCutoff;
uniform sampler2D uAlbedoMap;

void main()
{
    float alpha = uBaseColorAlpha;
    if (uHasAlbedoMap)
        alpha *= texture(uAlbedoMap, vUV).a;
    if (uAlphaMasked && alpha < uAlphaCutoff)
        discard;
}
