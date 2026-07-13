const vec3 ENGINE_BLOOM_LUMA = vec3(0.3, 0.59, 0.11);

float EngineBloomLuminance(vec3 color)
{
    return dot(color, ENGINE_BLOOM_LUMA);
}

vec3 EngineBloomAverage(vec3 a, vec3 b, vec3 c, vec3 d, bool karis)
{
    if (!karis)
        return (a + b + c + d) * 0.25;
    float wa = 1.0 / (1.0 + EngineBloomLuminance(a));
    float wb = 1.0 / (1.0 + EngineBloomLuminance(b));
    float wc = 1.0 / (1.0 + EngineBloomLuminance(c));
    float wd = 1.0 / (1.0 + EngineBloomLuminance(d));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}
