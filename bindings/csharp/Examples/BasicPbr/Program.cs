using System.Numerics;
using RenderingEngine;

Backend backend = args.Contains("--vulkan") ? Backend.Vulkan : Backend.OpenGL;
using var renderer = new Renderer(new RendererOptions
{
    Backend = backend,
    WindowTitle = $"Rendering Engine C# PBR - {backend}",
    Validation = args.Contains("--validation")
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
SceneObject obj = renderer.AddObject(sphere, bronze,
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

float angle = 0;
while (renderer.PumpEvents())
{
    angle += 0.75f;
    renderer.SetObjectTransform(obj,
        Renderer.Transform(new(0, 1, 0), new(0, angle, 0), Vector3.One));
    renderer.RenderFrame();
}
