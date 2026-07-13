#version 460 core

in vec2 vUv;
out vec4 FragColor;

uniform sampler2D uOverlay;

void main()
{
    // CPU rows use top-left origin; OpenGL texture coordinates use bottom-left.
    FragColor = texture(uOverlay, vec2(vUv.x, 1.0 - vUv.y));
}
