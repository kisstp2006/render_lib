#version 460 core

layout(location = 0) in vec3 aCorner;

uniform mat4 uViewProjection;
uniform vec3 uBoundsMinimum;
uniform vec3 uBoundsMaximum;
uniform bool uLineMode;

void main()
{
    vec3 worldPosition = uLineMode
        ? mix(uBoundsMinimum, uBoundsMaximum, aCorner.x)
        : mix(uBoundsMinimum, uBoundsMaximum, aCorner);
    gl_Position = uViewProjection * vec4(worldPosition, 1.0);
}
