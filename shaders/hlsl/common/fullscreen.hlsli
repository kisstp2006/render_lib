#ifndef ENGINE_FULLSCREEN_HLSLI
#define ENGINE_FULLSCREEN_HLSLI

float2 EngineFullscreenUv(uint vertexId)
{
    return float2(float((vertexId << 1u) & 2u), float(vertexId & 2u));
}

float2 EngineFullscreenNdc(uint vertexId)
{
    return EngineFullscreenUv(vertexId) * 2.0f - 1.0f.xx;
}

#endif
