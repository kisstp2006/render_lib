#version 460 core

in vec3 vWorldPos;
in vec2 vUV;

uniform vec3 uLightPosition;
uniform float uLightRange;
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
    gl_FragDepth = length(vWorldPos - uLightPosition) / uLightRange;
}
