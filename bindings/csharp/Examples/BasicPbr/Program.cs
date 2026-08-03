using System.Numerics;
using RenderingEngine;
using RenderingEngine.Silk;
using Silk.NET.Windowing;

Backend backend = args.Contains("--vulkan") ? Backend.Vulkan : Backend.OpenGL;
int frameLimit = 0;
int frameArg = Array.IndexOf(args, "--frames");
if (frameArg >= 0 && frameArg + 1 < args.Length)
    frameLimit = int.Parse(args[frameArg + 1]);
WindowOptions windowOptions = SilkRendererWindow.CreateOptions(backend,
    $"Rendering Engine C# PBR - Silk.NET - {backend}",
    visible: !args.Contains("--hidden"));
using IWindow window = Window.Create(windowOptions);

Renderer? renderer = null;
SceneObject sceneObject = default;
float angle = 0;
int renderedFrames = 0;

window.Load += () =>
{
    renderer = new Renderer(new RendererOptions
    {
        Backend = backend,
        Width = (uint)window.FramebufferSize.X,
        Height = (uint)window.FramebufferSize.Y,
        VSync = true,
        Validation = args.Contains("--validation"),
        WindowHost = new SilkWindowHost(window)
    });

    var camera = Camera.Default;
    camera.Position = new(0, 2.4f, 8);
    camera.PitchDegrees = -12;
    renderer.SetCamera(camera);

    var material = PbrMaterial.Default;
    material.Albedo = new(0.42f, 0.11f, 0.05f);
    material.Metallic = 0.82f;
    material.Roughness = 0.2f;
    Material bronze = renderer.CreateMaterial(material);
    Mesh sphere = renderer.CreateSphere();
    sceneObject = renderer.AddObject(sphere, bronze,
        Renderer.Transform(new(0, 1, 0), Vector3.Zero, Vector3.One));

    var floorMaterial = PbrMaterial.Default;
    floorMaterial.Albedo = new(0.28f);
    floorMaterial.Roughness = 0.85f;
    renderer.AddObject(renderer.CreateCube(), renderer.CreateMaterial(floorMaterial),
        Renderer.Transform(new(0, -0.25f, 0), Vector3.Zero, new(5, 0.2f, 5)));

    renderer.AddPointLight(new LocalPointLight
    {
        Position = new(-2.5f, 3.5f, 2), Color = new(1, 0.35f, 0.12f),
        Intensity = 85, Radius = 12
    });
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
    angle += (float)deltaSeconds * 45.0f;
    renderer.SetObjectTransform(sceneObject,
        Renderer.Transform(new(0, 1, 0), new(0, angle, 0), Vector3.One));
    renderer.RenderFrame((float)deltaSeconds);
    if (frameLimit > 0 && ++renderedFrames >= frameLimit)
        window.Close();
};

window.Closing += () =>
{
    renderer?.Dispose();
    renderer = null;
};

window.Run();
