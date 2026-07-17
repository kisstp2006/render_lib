static float4 gl_Position;
static int gl_VertexID;
static float2 vNdc;

struct SPIRV_Cross_Input
{
    uint gl_VertexID : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float2 vNdc : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float2 pos = (float2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0f) - 1.0f.xx;
    vNdc = pos;
    gl_Position = float4(pos, 1.0f, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexID = int(stage_input.gl_VertexID);
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vNdc = vNdc;
    return stage_output;
}
