const float ENV_PI = 3.14159265358979323846;

vec3 CubeDirection(uint face, vec2 uv)
{
    if (face == 0u) return normalize(vec3( 1.0, -uv.y, -uv.x));
    if (face == 1u) return normalize(vec3(-1.0, -uv.y,  uv.x));
    if (face == 2u) return normalize(vec3( uv.x,  1.0,  uv.y));
    if (face == 3u) return normalize(vec3( uv.x, -1.0, -uv.y));
    if (face == 4u) return normalize(vec3( uv.x, -uv.y,  1.0));
    return normalize(vec3(-uv.x, -uv.y, -1.0));
}

float RadicalInverseVdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint count)
{
    return vec2(float(i) / float(count), RadicalInverseVdc(i));
}

vec3 ImportanceSampleGgx(vec2 xi, vec3 normal, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * ENV_PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 halfVector = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * halfVector.x + bitangent * halfVector.y + normal * halfVector.z);
}
