using System.Numerics;
using System.Runtime.InteropServices;

namespace RenderingEngine;

public enum Backend : uint { OpenGL = 0, Vulkan = 1 }
public readonly record struct Mesh(ulong Value);
public readonly record struct Material(ulong Value);
public readonly record struct SceneObject(ulong Value);
public readonly record struct PointLight(ulong Value);

public struct RenderVertex
{
    public Vector3 Position;
    public Vector3 Normal;
    public Vector4 Tangent;
    public Vector2 UV;
}

public sealed class RendererOptions
{
    public Backend Backend { get; init; } = Backend.OpenGL;
    public string WindowTitle { get; init; } = "Rendering Engine C#";
    public uint Width { get; init; } = 1280;
    public uint Height { get; init; } = 720;
    public bool Resizable { get; init; } = true;
    public bool Visible { get; init; } = true;
    public bool VSync { get; init; } = true;
    public bool Validation { get; init; }
    public uint MsaaSamples { get; init; } = 4;
    public string ShaderDirectory { get; init; } = Path.Combine(AppContext.BaseDirectory, "shaders");
    public string PipelineCacheDirectory { get; init; } = string.Empty;
}

public struct PbrMaterial
{
    public Vector3 Albedo;
    public float Alpha;
    public float Metallic;
    public float Roughness;
    public Vector3 Emissive;
    public float AmbientOcclusion;
    public float SpecularF0;

    public static PbrMaterial Default => new()
    {
        Albedo = new(0.8f), Alpha = 1.0f, Roughness = 0.5f,
        AmbientOcclusion = 1.0f, SpecularF0 = 0.04f
    };
}

public struct Camera
{
    public Vector3 Position;
    public float YawDegrees, PitchDegrees, VerticalFovDegrees, NearPlane, FarPlane;
    public static Camera Default => new()
    {
        Position = new(0, 1.8f, 6), YawDegrees = -90, PitchDegrees = -10,
        VerticalFovDegrees = 60, NearPlane = 0.05f, FarPlane = 500
    };
}

public struct DirectionalLight
{
    public Vector3 Direction, Color;
    public float Intensity;
    public bool CastsShadows;
    public static DirectionalLight Default => new()
    {
        Direction = new(-0.4f, -0.85f, -0.35f), Color = new(1, 0.96f, 0.88f),
        Intensity = 3, CastsShadows = true
    };
}

public struct LocalPointLight
{
    public Vector3 Position, Color;
    public float Intensity, Radius;
    public bool CastsShadows;
}

public readonly record struct RendererFrameStats(float GpuMilliseconds,
    uint DrawBatches, uint Instances, uint DrawCallsSaved);

public sealed class Renderer : IDisposable
{
    private nint _handle;
    private readonly int _ownerThreadId = Environment.CurrentManagedThreadId;
    public const uint ApiVersion = 1;

    public Renderer(RendererOptions? options = null)
    {
        options ??= new RendererOptions();
        if (Native.re_get_api_version() != ApiVersion)
            throw new NotSupportedException("Rendering Engine native ABI version mismatch.");
        nint title = Marshal.StringToCoTaskMemUTF8(options.WindowTitle);
        nint shaders = Marshal.StringToCoTaskMemUTF8(options.ShaderDirectory);
        nint cache = options.PipelineCacheDirectory.Length == 0 ? 0
            : Marshal.StringToCoTaskMemUTF8(options.PipelineCacheDirectory);
        try
        {
            var desc = new Native.RendererDesc
            {
                StructSize = (uint)Marshal.SizeOf<Native.RendererDesc>(), Backend = options.Backend,
                WindowTitle = title, Width = options.Width, Height = options.Height,
                Resizable = options.Resizable ? 1 : 0, Visible = options.Visible ? 1 : 0,
                VSync = options.VSync ? 1 : 0, Validation = options.Validation ? 1 : 0,
                MsaaSamples = options.MsaaSamples, ShaderDirectory = shaders,
                PipelineCacheDirectory = cache
            };
            _handle = Native.re_renderer_create(in desc);
            if (_handle == 0) ThrowLastError("Could not create renderer");
        }
        finally
        {
            Marshal.FreeCoTaskMem(title);
            Marshal.FreeCoTaskMem(shaders);
            if (cache != 0) Marshal.FreeCoTaskMem(cache);
        }
    }

    public string BackendName => Marshal.PtrToStringUTF8(Native.re_renderer_backend_name(Handle)) ?? "Unknown";
    public bool ShouldClose => Native.re_renderer_should_close(Handle) != 0;
    public bool PumpEvents() => BoolResult(Native.re_renderer_pump_events(Handle));
    public bool Tick(float deltaSeconds = 1f / 60f) =>
        BoolResult(Native.re_renderer_tick(Handle, deltaSeconds));
    public void RenderFrame(float deltaSeconds = 1f / 60f) => Check(Native.re_renderer_render_frame(Handle, deltaSeconds));
    public void RequestClose() => Native.re_renderer_request_close(Handle);
    public void Resize(uint width, uint height) => Check(Native.re_renderer_resize(Handle, width, height));

    public Mesh CreateCube(float halfExtent = 1) => new(HandleResult(Native.re_renderer_create_cube(Handle, halfExtent)));
    public Mesh CreateSphere(float radius = 1, uint stacks = 32, uint slices = 32) =>
        new(HandleResult(Native.re_renderer_create_sphere(Handle, radius, stacks, slices)));
    public Mesh CreateMesh(ReadOnlySpan<RenderVertex> vertices, ReadOnlySpan<uint> indices)
    {
        var nativeVertices = new Native.Vertex[vertices.Length];
        for (int index = 0; index < vertices.Length; ++index)
            nativeVertices[index] = ToNative(vertices[index]);
        uint[] nativeIndices = indices.ToArray();
        return new(HandleResult(Native.re_renderer_create_mesh(Handle, nativeVertices,
            (nuint)nativeVertices.Length, nativeIndices, (nuint)nativeIndices.Length)));
    }
    public void DestroyMesh(Mesh mesh) => Native.re_renderer_destroy_mesh(Handle, mesh.Value);
    public Material CreateMaterial(PbrMaterial material) =>
        new(HandleResult(Native.re_renderer_create_material(Handle, ToNative(material))));
    public void UpdateMaterial(Material handle, PbrMaterial material) =>
        Check(Native.re_renderer_update_material(Handle, handle.Value, ToNative(material)));
    public void DestroyMaterial(Material material) =>
        Native.re_renderer_destroy_material(Handle, material.Value);
    public SceneObject AddObject(Mesh mesh, Material material, float[] transform) =>
        new(HandleResult(Native.re_renderer_add_object(Handle, mesh.Value, material.Value, Matrix(transform))));
    public void SetObjectTransform(SceneObject obj, float[] transform) =>
        Check(Native.re_renderer_set_object_transform(Handle, obj.Value, Matrix(transform)));
    public bool RemoveObject(SceneObject obj) => Native.re_renderer_remove_object(Handle, obj.Value) != 0;
    public void ClearObjects() => Native.re_renderer_clear_objects(Handle);
    public PointLight AddPointLight(LocalPointLight light) =>
        new(HandleResult(Native.re_renderer_add_point_light(Handle, ToNative(light))));
    public void SetPointLight(PointLight light, LocalPointLight value) =>
        Check(Native.re_renderer_set_point_light(Handle, light.Value, ToNative(value)));
    public bool RemovePointLight(PointLight light) =>
        Native.re_renderer_remove_point_light(Handle, light.Value) != 0;
    public void ClearPointLights() => Native.re_renderer_clear_point_lights(Handle);
    public void SetCamera(Camera camera) => Check(Native.re_renderer_set_camera(Handle, ToNative(camera)));
    public void SetSun(DirectionalLight light) => Check(Native.re_renderer_set_sun(Handle, ToNative(light)));
    public void SetExposure(float exposure) => Check(Native.re_renderer_set_exposure(Handle, exposure));
    public void SetBackgroundColor(Vector3 zenith, Vector3 horizon) =>
        Check(Native.re_renderer_set_background_color(Handle,
            [zenith.X, zenith.Y, zenith.Z], [horizon.X, horizon.Y, horizon.Z]));
    public void RequestScreenshot(string path) => Check(Native.re_renderer_request_screenshot(Handle, path));
    public RendererFrameStats GetFrameStats()
    {
        Check(Native.re_renderer_get_frame_stats(Handle, out var stats));
        return new(stats.GpuFrameMilliseconds, stats.DrawBatchCount, stats.InstanceCount, stats.DrawCallsSaved);
    }

    public static float[] Transform(Vector3 translation, Vector3 rotationDegrees, Vector3 scale)
    {
        float[] result = new float[16];
        Native.re_compose_transform([translation.X, translation.Y, translation.Z],
            [rotationDegrees.X, rotationDegrees.Y, rotationDegrees.Z],
            [scale.X, scale.Y, scale.Z], result);
        return result;
    }

    public void Dispose()
    {
        EnsureOwnerThread();
        if (_handle != 0) { Native.re_renderer_destroy(_handle); _handle = 0; }
    }

    private nint Handle
    {
        get
        {
            EnsureOwnerThread();
            return _handle != 0 ? _handle : throw new ObjectDisposedException(nameof(Renderer));
        }
    }
    private void EnsureOwnerThread()
    {
        if (Environment.CurrentManagedThreadId != _ownerThreadId)
            throw new InvalidOperationException(
                "Renderer creation, rendering and disposal must use the same thread.");
    }
    private static void Check(int success) { if (success == 0) ThrowLastError("Native renderer call failed"); }
    private static bool BoolResult(int value)
    {
        if (value != 0) return true;
        string? error = Marshal.PtrToStringUTF8(Native.re_get_last_error());
        if (!string.IsNullOrWhiteSpace(error)) throw new InvalidOperationException(error);
        return false;
    }
    private static ulong HandleResult(ulong value) { if (value == 0) ThrowLastError("Native renderer returned an invalid handle"); return value; }
    private static void ThrowLastError(string fallback)
    {
        string? error = Marshal.PtrToStringUTF8(Native.re_get_last_error());
        throw new InvalidOperationException(string.IsNullOrWhiteSpace(error) ? fallback : error);
    }
    private static float[] Matrix(float[] value) => value.Length == 16 ? value
        : throw new ArgumentException("A transform must contain 16 column-major floats.");
    private static Native.MaterialDesc ToNative(PbrMaterial m) => new()
    {
        AlbedoR = m.Albedo.X, AlbedoG = m.Albedo.Y, AlbedoB = m.Albedo.Z, Alpha = m.Alpha,
        Metallic = m.Metallic, Roughness = m.Roughness,
        EmissiveR = m.Emissive.X, EmissiveG = m.Emissive.Y, EmissiveB = m.Emissive.Z,
        AmbientOcclusion = m.AmbientOcclusion, SpecularF0 = m.SpecularF0
    };
    private static Native.CameraDesc ToNative(Camera c) => new()
    {
        PositionX = c.Position.X, PositionY = c.Position.Y, PositionZ = c.Position.Z,
        YawDegrees = c.YawDegrees, PitchDegrees = c.PitchDegrees,
        VerticalFovDegrees = c.VerticalFovDegrees, NearPlane = c.NearPlane, FarPlane = c.FarPlane
    };
    private static Native.DirectionalLightDesc ToNative(DirectionalLight l) => new()
    {
        DirectionX = l.Direction.X, DirectionY = l.Direction.Y, DirectionZ = l.Direction.Z,
        ColorR = l.Color.X, ColorG = l.Color.Y, ColorB = l.Color.Z,
        Intensity = l.Intensity, CastsShadows = l.CastsShadows ? 1 : 0
    };
    private static Native.PointLightDesc ToNative(LocalPointLight l) => new()
    {
        PositionX = l.Position.X, PositionY = l.Position.Y, PositionZ = l.Position.Z,
        ColorR = l.Color.X, ColorG = l.Color.Y, ColorB = l.Color.Z,
        Intensity = l.Intensity, Radius = l.Radius, CastsShadows = l.CastsShadows ? 1 : 0
    };
    private static Native.Vertex ToNative(RenderVertex v) => new()
    {
        PositionX = v.Position.X, PositionY = v.Position.Y, PositionZ = v.Position.Z,
        NormalX = v.Normal.X, NormalY = v.Normal.Y, NormalZ = v.Normal.Z,
        TangentX = v.Tangent.X, TangentY = v.Tangent.Y,
        TangentZ = v.Tangent.Z, TangentW = v.Tangent.W,
        U = v.UV.X, V = v.UV.Y
    };
}
