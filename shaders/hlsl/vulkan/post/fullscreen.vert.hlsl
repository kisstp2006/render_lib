#include "../../common/fullscreen.hlsli"

static float4 gl_Position;
static int gl_VertexIndex;
static float2 uv;

struct SPIRV_Cross_Input
{
    uint gl_VertexIndex : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float2 uv : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float2 position = EngineFullscreenUv(uint(gl_VertexIndex));
    uv = position;
    gl_Position = float4((position * 2.0f) - 1.0f.xx, 0.0f, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexIndex = int(stage_input.gl_VertexIndex);
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.uv = uv;
    return stage_output;
}
