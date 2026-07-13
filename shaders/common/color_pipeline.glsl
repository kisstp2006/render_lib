vec3 EngineUnchartedTonemap(vec3 color, vec4 curve0, vec3 curve1)
{
    vec3 numerator = color * (curve0.x * color + curve0.y * curve0.z)
                   + curve1.x * curve0.w;
    vec3 denominator = color * (curve0.x * color + curve0.y)
                     + curve1.y * curve0.w;
    return (numerator / denominator - curve1.x / curve1.y) * curve1.z;
}

vec3 EngineLinearToSrgb(vec3 color)
{
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

vec3 EngineSrgbToLinear(vec3 color)
{
    vec3 low = color / 12.92;
    vec3 high = pow((max(color, vec3(0.0)) + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), color));
}

float EngineHash12(vec2 position)
{
    vec3 p3 = fract(vec3(position.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
