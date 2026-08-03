using System.Numerics;
using System.Runtime.InteropServices;
using RenderingEngine;
using RenderingEngine.Silk;
using Silk.NET.Windowing;

Backend backend = args.Contains("--vulkan") ? Backend.Vulkan : Backend.OpenGL;
bool drawCustomPass = !args.Contains("--no-custom");
bool dispatchCompute = !args.Contains("--no-compute");
int frames = 0;
int frameArg = Array.IndexOf(args, "--frames");
if (frameArg >= 0 && frameArg + 1 < args.Length) frames = int.Parse(args[frameArg + 1]);
int screenshotArg = Array.IndexOf(args, "--screenshot");
string? screenshotPath = screenshotArg >= 0 && screenshotArg + 1 < args.Length
    ? Path.GetFullPath(args[screenshotArg + 1])
    : null;

WindowOptions windowOptions = SilkRendererWindow.CreateOptions(backend,
    $"Rendering Engine C# custom shader - Silk.NET - {backend}", 960, 540,
    visible: !args.Contains("--hidden"));
using IWindow window = Window.Create(windowOptions);
window.Initialize();

using var renderer = new Renderer(new RendererOptions
{
    Backend = backend,
    Width = (uint)window.FramebufferSize.X,
    Height = (uint)window.FramebufferSize.Y,
    VSync = false,
    Validation = args.Contains("--validation"),
    WindowHost = new SilkWindowHost(window)
});
window.FramebufferResize += size =>
{
    if (size.X > 0 && size.Y > 0)
        renderer.Resize((uint)size.X, (uint)size.Y);
};

PostProcessSettings initialPost = renderer.GetPostProcessSettings();
PostProcessSettings roundTripPost = initialPost;
roundTripPost.Exposure += 0.125f;
renderer.SetPostProcessSettings(roundTripPost);
if (MathF.Abs(renderer.GetPostProcessSettings().Exposure - roundTripPost.Exposure) > 0.0001f)
    throw new InvalidOperationException("Post-process settings round-trip failed");
renderer.SetPostProcessSettings(initialPost);

// Keep a real PBR scene behind the programmable pass. This makes the sample a
// regression test for the intended scene -> custom overlay -> UI/present order.
var camera = Camera.Default;
camera.Position = new(0, 2.4f, 8);
camera.PitchDegrees = -12;
renderer.SetCamera(camera);

var bronzePbr = PbrMaterial.Default;
bronzePbr.Albedo = new(0.42f, 0.11f, 0.05f);
bronzePbr.Metallic = 0.82f;
bronzePbr.Roughness = 0.2f;
renderer.AddObject(renderer.CreateSphere(), renderer.CreateMaterial(bronzePbr),
    Renderer.Transform(new(0, 1, 0), Vector3.Zero, Vector3.One));

var floorPbr = PbrMaterial.Default;
floorPbr.Albedo = new(0.28f);
floorPbr.Roughness = 0.85f;
renderer.AddObject(renderer.CreateCube(), renderer.CreateMaterial(floorPbr),
    Renderer.Transform(new(0, -0.25f, 0), Vector3.Zero, new(5, 0.2f, 5)));
renderer.AddPointLight(new LocalPointLight
{
    Position = new(-2.5f, 3.5f, 2),
    Color = new(1, 0.35f, 0.12f),
    Intensity = 85,
    Radius = 12
});

GraphicsDevice gpu = renderer.GraphicsDevice;
string shaderRoot = Path.Combine(AppContext.BaseDirectory, "custom-shaders");

ShaderModule vertexShader = gpu.CreateShaderModule(new ShaderModuleOptions
{
    Stage = ShaderStage.Vertex,
    SourcePath = Path.Combine(shaderRoot, "custom.hlsl"),
    EntryPoint = "VSMain",
    DebugName = "C# custom vertex"
});
ShaderModule fragmentShader = gpu.CreateShaderModule(new ShaderModuleOptions
{
    Stage = ShaderStage.Fragment,
    SourcePath = Path.Combine(shaderRoot, "custom.hlsl"),
    EntryPoint = "PSMain",
    DebugName = "C# custom fragment"
});
ShaderModule fragmentPermutation = gpu.CreateShaderPermutation(fragmentShader,
    [new ShaderDefine("CUSTOM_VARIANT", "1")]);
ShaderModule glslShader = gpu.CreateShaderModule(new ShaderModuleOptions
{
    Stage = ShaderStage.Vertex,
    Language = ShaderLanguage.Glsl,
    SourcePath = "inline_csharp.vert.glsl",
    Source = """
        #version 450
        layout(location = 0) in vec2 Position;
        void main() { gl_Position = vec4(Position, 0.0, 1.0); }
        """,
    EnableHotReload = false,
    DebugName = "C# inline GLSL"
});
ShaderModule computeShader = gpu.CreateShaderModule(new ShaderModuleOptions
{
    Stage = ShaderStage.Compute,
    SourcePath = Path.Combine(shaderRoot, "compute.hlsl"),
    EntryPoint = "CSMain",
    DebugName = "C# custom compute"
});

var layout = new VertexBinding(0, (uint)Marshal.SizeOf<CustomVertex>());
VertexAttribute[] attributes =
[
    new(0, 0, VertexFormat.Float2, 0),
    new(1, 0, VertexFormat.Float2, 8)
];
GraphicsPipeline presentPipeline = gpu.CreateGraphicsPipeline(new GraphicsPipelineOptions
{
    VertexShader = vertexShader,
    FragmentShader = fragmentPermutation,
    VertexBindings = [layout],
    VertexAttributes = attributes,
    Cull = CullMode.None,
    DebugName = "C# presentation pipeline"
});
GraphicsPipeline offscreenPipeline = gpu.CreateGraphicsPipeline(new GraphicsPipelineOptions
{
    VertexShader = vertexShader,
    FragmentShader = fragmentShader,
    VertexBindings = [layout],
    VertexAttributes = attributes,
    ColorFormats = [TextureFormat.RGBA8Unorm],
    Cull = CullMode.None,
    DebugName = "C# offscreen pipeline"
});
ComputePipeline computePipeline = gpu.CreateComputePipeline(new ComputePipelineOptions
{
    ComputeShader = computeShader,
    DebugName = "C# compute pipeline"
});

CustomVertex[] vertices =
[
    new(0.35f, -0.85f, 0, 1),
    new(0.90f, -0.85f, 1, 1),
    new(0.625f, -0.30f, 0.5f, 0)
];
CustomVertex[] offscreenVertices =
[
    new(-1, -1, 0, 1), new( 1, -1, 1, 1), new( 1,  1, 1, 0),
    new(-1, -1, 0, 1), new( 1,  1, 1, 0), new(-1,  1, 0, 0)
];
GpuBuffer vertexBuffer = gpu.CreateBuffer<CustomVertex>(new BufferOptions
{
    Usage = BufferUsage.Vertex,
    DebugName = "C# custom vertices"
}, vertices.AsSpan());
GpuBuffer offscreenVertexBuffer = gpu.CreateBuffer<CustomVertex>(new BufferOptions
{
    Usage = BufferUsage.Vertex,
    DebugName = "C# offscreen fullscreen vertices"
}, offscreenVertices.AsSpan());
Vector4[] tint = [new(0.25f, 0.85f, 1.0f, 1.0f)];
GpuBuffer uniformBuffer = gpu.CreateBuffer<Vector4>(new BufferOptions
{
    Usage = BufferUsage.Uniform,
    CpuWritable = true,
    DebugName = "C# tint uniform"
}, tint.AsSpan());
uint[] computeData = new uint[4];
GpuBuffer storageBuffer = gpu.CreateBuffer<uint>(new BufferOptions
{
    Usage = BufferUsage.Storage,
    CpuWritable = true,
    DebugName = "C# compute storage"
}, computeData.AsSpan());

byte[] checker =
[
    255, 80, 35, 255, 35, 80, 255, 255,
    35, 80, 255, 255, 255, 80, 35, 255
];
Texture checkerTexture = gpu.CreateTexture(new TextureOptions
{
    Width = 2,
    Height = 2,
    Format = TextureFormat.RGBA8Unorm,
    Usage = TextureUsage.Sampled | TextureUsage.TransferDestination,
    DebugName = "C# checker"
}, checker);
Sampler sampler = gpu.CreateSampler();
Texture offscreenTexture = gpu.CreateTexture(new TextureOptions
{
    Width = 960,
    Height = 540,
    Format = TextureFormat.RGBA8Unorm,
    Usage = TextureUsage.ColorAttachment | TextureUsage.Sampled,
    DebugName = "C# offscreen color"
});
RenderTarget offscreenTarget = gpu.CreateRenderTarget(new RenderTargetOptions
{
    ColorAttachments = [offscreenTexture],
    DebugName = "C# offscreen target"
});

ShaderReflection reflection = gpu.GetShaderReflection(fragmentShader);
ShaderReflection glslReflection = gpu.GetShaderReflection(glslShader);
Console.WriteLine($"{renderer.BackendName}: fragment reflection has {reflection.Resources.Count} resources; " +
    $"GLSL reflection has {glslReflection.Resources.Count}");

for (int frame = 0; !window.IsClosing && (frames == 0 || frame < frames); ++frame)
{
    window.DoEvents();
    if (window.IsClosing) break;
    gpu.ReloadChangedShaders();

    if (drawCustomPass)
    {
        if (dispatchCompute)
        {
            gpu.BindComputePipeline(computePipeline);
            gpu.BindStorageBuffer(0, 0, storageBuffer);
            gpu.Dispatch(4);
        }

        gpu.BeginRenderPass(new RenderPassOptions
        {
            Target = offscreenTarget,
            ColorLoad = LoadAction.Clear,
            ClearColor = [0.02f, 0.025f, 0.04f, 1]
        });
        gpu.BindGraphicsPipeline(offscreenPipeline);
        gpu.BindVertexBuffer(0, offscreenVertexBuffer);
        gpu.BindUniformBuffer(0, 0, uniformBuffer);
        gpu.BindTexture(0, 1, checkerTexture, sampler);
        gpu.BindSampler(0, 2, sampler);
        gpu.Draw(6);
        gpu.EndRenderPass();

        gpu.BeginRenderPass(new RenderPassOptions { ColorLoad = LoadAction.Load });
        gpu.BindGraphicsPipeline(presentPipeline);
        gpu.BindVertexBuffer(0, vertexBuffer);
        gpu.BindUniformBuffer(0, 0, uniformBuffer);
        gpu.BindTexture(0, 1, offscreenTexture, sampler);
        gpu.BindSampler(0, 2, sampler);
        gpu.Draw(3);
        gpu.EndRenderPass();
    }
    if (screenshotPath is not null && frames > 0 && frame == frames - 1)
        renderer.RequestScreenshot(screenshotPath);
    renderer.RenderFrame();
}

gpu.DestroyRenderTarget(offscreenTarget);
gpu.DestroyTexture(offscreenTexture);
gpu.DestroySampler(sampler);
gpu.DestroyTexture(checkerTexture);
gpu.DestroyBuffer(storageBuffer);
gpu.DestroyBuffer(uniformBuffer);
gpu.DestroyBuffer(offscreenVertexBuffer);
gpu.DestroyBuffer(vertexBuffer);
gpu.DestroyComputePipeline(computePipeline);
gpu.DestroyGraphicsPipeline(offscreenPipeline);
gpu.DestroyGraphicsPipeline(presentPipeline);
gpu.DestroyShaderModule(computeShader);
gpu.DestroyShaderModule(glslShader);
gpu.DestroyShaderModule(fragmentPermutation);
gpu.DestroyShaderModule(fragmentShader);
gpu.DestroyShaderModule(vertexShader);
Console.WriteLine($"{renderer.BackendName}: C# custom graphics smoke passed");

[StructLayout(LayoutKind.Sequential)]
readonly record struct CustomVertex(float X, float Y, float U, float V);
