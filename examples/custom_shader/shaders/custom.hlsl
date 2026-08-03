cbuffer TintBuffer : register(b0, space0)
{
    float4 Tint;
};

[[vk::binding(1, 0)]] Texture2D InputTexture;
[[vk::binding(2, 0)]] SamplerState InputSampler;

struct VertexInput
{
    [[vk::location(0)]] float2 Position : POSITION;
    [[vk::location(1)]] float2 Uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 Uv : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.Position = float4(input.Position, 0.0, 1.0);
    output.Uv = input.Uv;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return InputTexture.Sample(InputSampler, input.Uv) * Tint;
}

