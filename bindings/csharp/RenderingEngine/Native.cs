using System.Runtime.InteropServices;

namespace RenderingEngine;

internal static class Native
{
    internal const string Library = "RenderingEngine";

    [StructLayout(LayoutKind.Sequential)]
    internal struct RendererDesc
    {
        internal uint StructSize;
        internal Backend Backend;
        internal nint WindowTitle;
        internal uint Width;
        internal uint Height;
        internal int Resizable;
        internal int Visible;
        internal int VSync;
        internal int Validation;
        internal uint MsaaSamples;
        internal nint ShaderDirectory;
        internal nint PipelineCacheDirectory;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct MaterialDesc
    {
        internal float AlbedoR, AlbedoG, AlbedoB, Alpha;
        internal float Metallic, Roughness;
        internal float EmissiveR, EmissiveG, EmissiveB;
        internal float AmbientOcclusion, SpecularF0;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Vertex
    {
        internal float PositionX, PositionY, PositionZ;
        internal float NormalX, NormalY, NormalZ;
        internal float TangentX, TangentY, TangentZ, TangentW;
        internal float U, V;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct CameraDesc
    {
        internal float PositionX, PositionY, PositionZ;
        internal float YawDegrees, PitchDegrees, VerticalFovDegrees;
        internal float NearPlane, FarPlane;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DirectionalLightDesc
    {
        internal float DirectionX, DirectionY, DirectionZ;
        internal float ColorR, ColorG, ColorB;
        internal float Intensity;
        internal int CastsShadows;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct PointLightDesc
    {
        internal float PositionX, PositionY, PositionZ;
        internal float ColorR, ColorG, ColorB;
        internal float Intensity, Radius;
        internal int CastsShadows;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct FrameStats
    {
        internal float GpuFrameMilliseconds;
        internal uint DrawBatchCount, InstanceCount, DrawCallsSaved;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint re_get_api_version();
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint re_get_last_error();
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint re_renderer_create(in RendererDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_destroy(nint renderer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_pump_events(nint renderer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_tick(nint renderer, float deltaSeconds);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_render_frame(nint renderer, float deltaSeconds);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_request_close(nint renderer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_should_close(nint renderer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_resize(nint renderer, uint width, uint height);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint re_renderer_backend_name(nint renderer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_get_frame_stats(nint renderer, out FrameStats stats);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_cube(nint renderer, float halfExtent);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_sphere(nint renderer, float radius, uint stacks, uint slices);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_mesh(nint renderer,
        [In] Vertex[] vertices, nuint vertexCount, [In] uint[] indices, nuint indexCount);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_destroy_mesh(nint renderer, ulong mesh);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_material(nint renderer, in MaterialDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_update_material(nint renderer, ulong material, in MaterialDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_destroy_material(nint renderer, ulong material);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_add_object(nint renderer, ulong mesh, ulong material,
        [In] float[] transform);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_set_object_transform(nint renderer, ulong obj,
        [In] float[] transform);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_remove_object(nint renderer, ulong obj);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_clear_objects(nint renderer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_add_point_light(nint renderer, in PointLightDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_set_point_light(nint renderer, ulong light,
        in PointLightDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_remove_point_light(nint renderer, ulong light);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_clear_point_lights(nint renderer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_set_camera(nint renderer, in CameraDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_set_sun(nint renderer, in DirectionalLightDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_set_exposure(nint renderer, float exposure);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_set_background_color(nint renderer,
        [In] float[] zenith, [In] float[] horizon);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_request_screenshot(nint renderer,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_compose_transform([In] float[] translation,
        [In] float[] rotationDegrees, [In] float[] scale, [Out] float[] transform);
}
