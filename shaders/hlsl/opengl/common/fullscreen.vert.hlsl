#include "../../common/fullscreen.hlsli"

static float4 gl_Position;
static int gl_VertexID;
static float2 vNdc;
static float2 vUv;

struct SPIRV_Cross_Input
{
    uint gl_VertexID : SV_VertexID;
};

struct SPIRV_Cross_Output
{
#ifdef ENGINE_OPENGL
    [[vk::location(0)]]
#endif
    float2 vNdc : TEXCOORD0;
#ifdef ENGINE_OPENGL
    [[vk::location(1)]]
#endif
    float2 vUv : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float2 pos = EngineFullscreenNdc(uint(gl_VertexID));
    vNdc = pos;
    vUv = (pos * 0.5f) + 0.5f.xx;
    gl_Position = float4(pos, 0.0f, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexID = int(stage_input.gl_VertexID);
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vNdc = vNdc;
    stage_output.vUv = vUv;
    return stage_output;
}
