float EngineLinearizeDepth(float depth, float nearPlane, float farPlane)
{
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane)
         / max(farPlane + nearPlane - ndc * (farPlane - nearPlane), 1e-6);
}

vec3 EngineRgbToYCoCg(vec3 color)
{
    return vec3(color.r * 0.25 + color.g * 0.5 + color.b * 0.25,
                color.r * 0.5 - color.b * 0.5,
               -color.r * 0.25 + color.g * 0.5 - color.b * 0.25);
}

vec3 EngineYCoCgToRgb(vec3 color)
{
    return vec3(color.x + color.y - color.z,
                color.x + color.z,
                color.x - color.y - color.z);
}
