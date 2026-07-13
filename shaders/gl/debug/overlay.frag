#version 460 core

in vec2 vUv;
out vec4 FragColor;

uniform sampler2D uOverlay;
uniform vec4 uUvRect;

void main()
{
    // CPU rows use top-left origin; OpenGL texture coordinates use bottom-left.
    vec2 localUv = vec2(vUv.x, 1.0 - vUv.y);
    FragColor = texture(uOverlay, uUvRect.xy + localUv * uUvRect.zw);
}
