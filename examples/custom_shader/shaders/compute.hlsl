[[vk::binding(0, 0)]] RWStructuredBuffer<uint> Output;

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    Output[id.x] = id.x + 1;
}

