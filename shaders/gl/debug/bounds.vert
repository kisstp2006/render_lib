#version 460 core

layout(location = 0) in vec3 aCorner;

uniform mat4 uViewProjection;
uniform vec3 uBoundsMinimum;
uniform vec3 uBoundsMaximum;

void main()
{
    vec3 worldPosition = mix(uBoundsMinimum, uBoundsMaximum, aCorner);
    gl_Position = uViewProjection * vec4(worldPosition, 1.0);
}
