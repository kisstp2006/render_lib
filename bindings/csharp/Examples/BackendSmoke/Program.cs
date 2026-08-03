using System.Numerics;
using RenderingEngine;
using RenderingEngine.Silk;
using Silk.NET.Windowing;

Backend backend = args.Contains("--vulkan") ? Backend.Vulkan : Backend.OpenGL;
int frames = 8;
int frameArg = Array.IndexOf(args, "--frames");
if (frameArg >= 0 && frameArg + 1 < args.Length)
    frames = int.Parse(args[frameArg + 1]);
string? screenshot = null;
int shotArg = Array.IndexOf(args, "--screenshot");
if (shotArg >= 0 && shotArg + 1 < args.Length)
    screenshot = args[shotArg + 1];

WindowOptions options = SilkRendererWindow.CreateOptions(backend,
    $"Rendering Engine Silk.NET smoke - {backend}", 640, 360,
    visible: !args.Contains("--hidden"));
using IWindow window = Window.Create(options);
Renderer? renderer = null;
int renderedFrames = 0;

window.Load += () =>
{
    renderer = new Renderer(new RendererOptions
    {
        Backend = backend,
        Width = (uint)window.FramebufferSize.X,
        Height = (uint)window.FramebufferSize.Y,
        VSync = false,
        Validation = args.Contains("--validation"),
        WindowHost = new SilkWindowHost(window)
    });
    var material = PbrMaterial.Default;
    material.Albedo = new(0.15f, 0.42f, 0.78f);
    material.Metallic = 0.65f;
    material.Roughness = 0.28f;
    renderer.AddObject(renderer.CreateSphere(), renderer.CreateMaterial(material),
        Renderer.Transform(Vector3.Zero, Vector3.Zero, Vector3.One));
};

window.FramebufferResize += size =>
{
    if (renderer is not null && size.X > 0 && size.Y > 0)
        renderer.Resize((uint)size.X, (uint)size.Y);
};

window.Render += deltaSeconds =>
{
    if (renderer is null)
        return;
    ++renderedFrames;
    if (screenshot is not null && renderedFrames == frames)
        renderer.RequestScreenshot(screenshot);
    renderer.RenderFrame((float)deltaSeconds);
    if (renderedFrames >= frames)
        window.Close();
};

window.Closing += () =>
{
    if (renderer is not null)
    {
        Console.WriteLine($"{renderer.BackendName}: Silk.NET C# smoke passed ({renderedFrames} frames)");
        renderer.Dispose();
        renderer = null;
    }
};

window.Run();
