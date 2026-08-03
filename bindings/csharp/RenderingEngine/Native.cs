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
        internal nint ExternalWindow;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ExternalWindowDesc
    {
        internal nint UserData;
        internal nint ShouldClose;
        internal nint RequestClose;
        internal nint PollEvents;
        internal nint MakeContextCurrent;
        internal nint GetGlProcAddress;
        internal nint SwapBuffers;
        internal nint SetSwapInterval;
        internal nint GetVulkanInstanceExtensions;
        internal nint CreateVulkanSurface;
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

    [StructLayout(LayoutKind.Sequential)]
    internal struct PostProcessSettings
    {
        internal uint StructSize;
        internal int Enabled;
        internal float Exposure;
        internal float BloomStrength;
        internal float BloomThreshold;
        internal float ShoulderStrength;
        internal float LinearStrength;
        internal float LinearAngle;
        internal float ToeStrength;
        internal float ToeNumerator;
        internal float ToeDenominator;
        internal float WhitePoint;
        internal int AutoExposure;
        internal float AutoExposureKey;
        internal float AutoExposureMin;
        internal float AutoExposureMax;
        internal float AutoExposureSpeed;
        internal float Saturation;
        internal float Contrast;
        internal float ColorTintR, ColorTintG, ColorTintB;
        internal float ColorLutWeight;
        internal int AntiAliasing;
        internal float FxaaSubpixel;
        internal float FxaaEdgeThreshold;
        internal float FxaaEdgeThresholdMin;
        internal float TaaHistoryWeight;
        internal float TaaSharpen;
        internal float TaaJitterScale;
        internal float TaaDepthThreshold;
        internal int LogPerformance;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ShaderDefine
    {
        internal nint Name;
        internal nint Value;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ShaderModuleDesc
    {
        internal uint StructSize;
        internal ShaderStage Stage;
        internal ShaderLanguage Language;
        internal nint SourcePath;
        internal nint SourceData;
        internal nuint SourceSize;
        internal nint EntryPoint;
        internal nint Defines;
        internal nuint DefineCount;
        internal int Optimize;
        internal int GenerateDebugInfo;
        internal int EnableHotReload;
        internal nint DebugName;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct ShaderReflectionInfo
    {
        internal ShaderStage Stage;
        internal fixed byte EntryPoint[64];
        internal fixed byte PermutationKey[65];
        internal ulong Generation;
        internal uint ResourceCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct ShaderResource
    {
        internal fixed byte Name[128];
        internal ShaderResourceType Type;
        internal uint Set;
        internal uint Binding;
        internal uint Location;
        internal uint ArrayCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct VertexBindingDesc
    {
        internal uint Binding;
        internal uint Stride;
        internal VertexInputRate InputRate;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct VertexAttributeDesc
    {
        internal uint Location;
        internal uint Binding;
        internal VertexFormat Format;
        internal uint Offset;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GraphicsPipelineDesc
    {
        internal uint StructSize;
        internal ulong VertexShader;
        internal ulong FragmentShader;
        internal nint VertexBindings;
        internal nuint VertexBindingCount;
        internal nint VertexAttributes;
        internal nuint VertexAttributeCount;
        internal PrimitiveTopology Topology;
        internal CullMode Cull;
        internal FrontFace Winding;
        internal int DepthTest;
        internal int DepthWrite;
        internal CompareOperation DepthCompare;
        internal int BlendEnabled;
        internal nint ColorFormats;
        internal nuint ColorFormatCount;
        internal int HasDepthFormat;
        internal TextureFormat DepthFormat;
        internal nint DebugName;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ComputePipelineDesc
    {
        internal uint StructSize;
        internal ulong ComputeShader;
        internal nint DebugName;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct BufferDesc
    {
        internal uint StructSize;
        internal ulong Size;
        internal BufferUsage Usage;
        internal int CpuWritable;
        internal nint DebugName;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TextureDesc
    {
        internal uint StructSize;
        internal uint Width;
        internal uint Height;
        internal uint MipLevels;
        internal TextureFormat Format;
        internal TextureUsage Usage;
        internal nint DebugName;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SamplerDesc
    {
        internal uint StructSize;
        internal Filter MinFilter;
        internal Filter MagFilter;
        internal AddressMode AddressU;
        internal AddressMode AddressV;
        internal AddressMode AddressW;
        internal float MaxAnisotropy;
        internal nint DebugName;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct RenderTargetDesc
    {
        internal uint StructSize;
        internal nint ColorAttachments;
        internal nuint ColorAttachmentCount;
        internal ulong DepthAttachment;
        internal nint DebugName;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct RenderPassDesc
    {
        internal uint StructSize;
        internal ulong Target;
        internal LoadAction ColorLoad;
        internal StoreAction ColorStore;
        internal fixed float ClearColor[4];
        internal LoadAction DepthLoad;
        internal StoreAction DepthStore;
        internal float ClearDepth;
        internal nint DebugName;
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
    internal static extern void re_post_process_settings_init(ref PostProcessSettings settings);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_set_post_process_settings(
        nint renderer, in PostProcessSettings settings);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_get_post_process_settings(
        nint renderer, ref PostProcessSettings settings);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_set_background_color(nint renderer,
        [In] float[] zenith, [In] float[] horizon);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_request_screenshot(nint renderer,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_shader_module(nint renderer, in ShaderModuleDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_shader_permutation(nint renderer, ulong shader,
        nint defines, nuint defineCount);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_destroy_shader_module(nint renderer, ulong shader);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_get_shader_reflection(nint renderer, ulong shader,
        out ShaderReflectionInfo reflection);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_get_shader_resource(nint renderer, ulong shader, uint index,
        out ShaderResource resource);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_graphics_pipeline(nint renderer,
        in GraphicsPipelineDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_compute_pipeline(nint renderer,
        in ComputePipelineDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_destroy_graphics_pipeline(nint renderer, ulong pipeline);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_destroy_compute_pipeline(nint renderer, ulong pipeline);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_buffer(nint renderer, in BufferDesc desc,
        nint initialData, nuint initialSize);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_update_buffer(nint renderer, ulong buffer, ulong offset,
        nint data, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_destroy_buffer(nint renderer, ulong buffer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_texture(nint renderer, in TextureDesc desc,
        nint initialData, nuint initialSize);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_update_texture(nint renderer, ulong texture, uint mipLevel,
        nint data, nuint size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_destroy_texture(nint renderer, ulong texture);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_sampler(nint renderer, in SamplerDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_destroy_sampler(nint renderer, ulong sampler);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong re_renderer_create_render_target(nint renderer, in RenderTargetDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_destroy_render_target(nint renderer, ulong target);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_begin_render_pass(nint renderer, in RenderPassDesc desc);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_end_render_pass(nint renderer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_bind_graphics_pipeline(nint renderer, ulong pipeline);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_bind_compute_pipeline(nint renderer, ulong pipeline);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_bind_vertex_buffer(nint renderer, uint binding, ulong buffer, ulong offset);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_bind_index_buffer(nint renderer, ulong buffer, IndexType type, ulong offset);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_bind_uniform_buffer(nint renderer, uint set, uint binding,
        ulong buffer, ulong offset, ulong size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_bind_storage_buffer(nint renderer, uint set, uint binding,
        ulong buffer, ulong offset, ulong size);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_bind_texture(nint renderer, uint set, uint binding,
        ulong texture, ulong sampler);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_bind_sampler(nint renderer, uint set, uint binding, ulong sampler);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_bind_storage_texture(nint renderer, uint set, uint binding, ulong texture);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_draw(nint renderer, uint vertexCount, uint instanceCount,
        uint firstVertex, uint firstInstance);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_draw_indexed(nint renderer, uint indexCount, uint instanceCount,
        uint firstIndex, int vertexOffset, uint firstInstance);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int re_renderer_dispatch(nint renderer, uint x, uint y, uint z);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_renderer_reset_graphics_commands(nint renderer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint re_renderer_reload_changed_shaders(nint renderer);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void re_compose_transform([In] float[] translation,
        [In] float[] rotationDegrees, [In] float[] scale, [Out] float[] transform);
}
