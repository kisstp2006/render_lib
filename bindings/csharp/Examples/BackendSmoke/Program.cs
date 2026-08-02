using System.Numerics;
using RenderingEngine;

Backend backend = args.Contains("--vulkan") ? Backend.Vulkan : Backend.OpenGL;
int frames = 8;
int frameArg = Array.IndexOf(args, "--frames");
if (frameArg >= 0 && frameArg + 1 < args.Length) frames = int.Parse(args[frameArg + 1]);
string? screenshot = null;
int shotArg = Array.IndexOf(args, "--screenshot");
if (shotArg >= 0 && shotArg + 1 < args.Length) screenshot = args[shotArg + 1];

using var renderer = new Renderer(new RendererOptions
{
    Backend = backend, Visible = false, VSync = false,
    Validation = args.Contains("--validation"), Width = 640, Height = 360
});
var material = PbrMaterial.Default;
material.Albedo = new(0.15f, 0.42f, 0.78f);
material.Metallic = 0.65f;
material.Roughness = 0.28f;
renderer.AddObject(renderer.CreateSphere(), renderer.CreateMaterial(material),
    Renderer.Transform(Vector3.Zero, Vector3.Zero, Vector3.One));

for (int frame = 0; frame < frames; ++frame)
{
    if (!renderer.PumpEvents()) throw new InvalidOperationException("Window closed during smoke test.");
    if (screenshot is not null && frame == frames - 1) renderer.RequestScreenshot(screenshot);
    renderer.RenderFrame();
}
Console.WriteLine($"{renderer.BackendName}: C# smoke passed ({frames} frames)");
