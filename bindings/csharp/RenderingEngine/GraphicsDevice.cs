using System.Runtime.InteropServices;
using System.Text;

namespace RenderingEngine;

public enum ShaderLanguage : uint { Hlsl, Glsl, SpirV }
public enum ShaderStage : uint { Vertex, Fragment, Compute }
public enum ShaderResourceType : uint
{
    Unknown, VertexInput, UniformBuffer, StorageBuffer, SampledTexture,
    CombinedTextureSampler, StorageTexture, Sampler, PushConstants
}
public enum VertexFormat : uint
{
    Float, Float2, Float3, Float4, UInt, UInt2, UInt4, UByte4Normalized
}
public enum VertexInputRate : uint { PerVertex, PerInstance }
public enum PrimitiveTopology : uint { TriangleList, TriangleStrip, LineList, PointList }
public enum CullMode : uint { None, Front, Back }
public enum FrontFace : uint { CounterClockwise, Clockwise }
public enum CompareOperation : uint { Never, Less, LessEqual, Equal, GreaterEqual, Greater, Always }
public enum TextureFormat : uint
{
    R8Unorm, RG8Unorm, RGBA8Unorm, RGBA8Srgb, RGBA16Float, R32Float, Depth32Float
}
[Flags]
public enum BufferUsage : uint
{
    Vertex = 1 << 0, Index = 1 << 1, Uniform = 1 << 2, Storage = 1 << 3,
    TransferSource = 1 << 4, TransferDestination = 1 << 5
}
[Flags]
public enum TextureUsage : uint
{
    Sampled = 1 << 0, Storage = 1 << 1, ColorAttachment = 1 << 2,
    DepthAttachment = 1 << 3, TransferSource = 1 << 4, TransferDestination = 1 << 5
}
public enum Filter : uint { Nearest, Linear }
public enum AddressMode : uint { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder }
public enum LoadAction : uint { Load, Clear, DontCare }
public enum StoreAction : uint { Store, DontCare }
public enum IndexType : uint { UInt16, UInt32 }

public readonly record struct ShaderModule(ulong Value);
public readonly record struct GraphicsPipeline(ulong Value);
public readonly record struct ComputePipeline(ulong Value);
public readonly record struct GpuBuffer(ulong Value);
public readonly record struct Texture(ulong Value);
public readonly record struct Sampler(ulong Value);
public readonly record struct RenderTarget(ulong Value);

public readonly record struct ShaderDefine(string Name, string Value = "1");
public readonly record struct VertexBinding(uint Binding, uint Stride,
    VertexInputRate InputRate = VertexInputRate.PerVertex);
public readonly record struct VertexAttribute(uint Location, uint Binding,
    VertexFormat Format, uint Offset);
public readonly record struct ShaderResource(string Name, ShaderResourceType Type,
    uint Set, uint Binding, uint Location, uint ArrayCount);
public sealed record ShaderReflection(ShaderStage Stage, string EntryPoint,
    string PermutationKey, ulong Generation, IReadOnlyList<ShaderResource> Resources);

public sealed class ShaderModuleOptions
{
    public required ShaderStage Stage { get; init; }
    public ShaderLanguage Language { get; init; } = ShaderLanguage.Hlsl;
    public string? SourcePath { get; init; }
    public string? Source { get; init; }
    public ReadOnlyMemory<byte> SourceBytes { get; init; }
    public string EntryPoint { get; init; } = "main";
    public IReadOnlyList<ShaderDefine> Defines { get; init; } = [];
    public bool Optimize { get; init; } = true;
    public bool GenerateDebugInfo { get; init; }
    public bool EnableHotReload { get; init; } = true;
    public string DebugName { get; init; } = string.Empty;
}

public sealed class GraphicsPipelineOptions
{
    public required ShaderModule VertexShader { get; init; }
    public required ShaderModule FragmentShader { get; init; }
    public IReadOnlyList<VertexBinding> VertexBindings { get; init; } = [];
    public IReadOnlyList<VertexAttribute> VertexAttributes { get; init; } = [];
    public PrimitiveTopology Topology { get; init; } = PrimitiveTopology.TriangleList;
    public CullMode Cull { get; init; } = CullMode.Back;
    public FrontFace Winding { get; init; } = FrontFace.CounterClockwise;
    public bool DepthTest { get; init; }
    public bool DepthWrite { get; init; }
    public CompareOperation DepthCompare { get; init; } = CompareOperation.Less;
    public bool BlendEnabled { get; init; }
    public IReadOnlyList<TextureFormat> ColorFormats { get; init; } = [];
    public TextureFormat? DepthFormat { get; init; }
    public string DebugName { get; init; } = string.Empty;
}

public sealed class ComputePipelineOptions
{
    public required ShaderModule ComputeShader { get; init; }
    public string DebugName { get; init; } = string.Empty;
}

public sealed class BufferOptions
{
    public ulong Size { get; init; }
    public BufferUsage Usage { get; init; } = BufferUsage.Vertex;
    public bool CpuWritable { get; init; }
    public string DebugName { get; init; } = string.Empty;
}

public sealed class TextureOptions
{
    public uint Width { get; init; } = 1;
    public uint Height { get; init; } = 1;
    public uint MipLevels { get; init; } = 1;
    public TextureFormat Format { get; init; } = TextureFormat.RGBA8Unorm;
    public TextureUsage Usage { get; init; } = TextureUsage.Sampled;
    public string DebugName { get; init; } = string.Empty;
}

public sealed class SamplerOptions
{
    public Filter MinFilter { get; init; } = Filter.Linear;
    public Filter MagFilter { get; init; } = Filter.Linear;
    public AddressMode AddressU { get; init; } = AddressMode.Repeat;
    public AddressMode AddressV { get; init; } = AddressMode.Repeat;
    public AddressMode AddressW { get; init; } = AddressMode.Repeat;
    public float MaxAnisotropy { get; init; } = 1;
    public string DebugName { get; init; } = string.Empty;
}

public sealed class RenderTargetOptions
{
    public IReadOnlyList<Texture> ColorAttachments { get; init; } = [];
    public Texture DepthAttachment { get; init; }
    public string DebugName { get; init; } = string.Empty;
}

public sealed class RenderPassOptions
{
    public RenderTarget Target { get; init; }
    public LoadAction ColorLoad { get; init; } = LoadAction.Load;
    public StoreAction ColorStore { get; init; } = StoreAction.Store;
    public float[] ClearColor { get; init; } = [0, 0, 0, 1];
    public LoadAction DepthLoad { get; init; } = LoadAction.Clear;
    public StoreAction DepthStore { get; init; } = StoreAction.Store;
    public float ClearDepth { get; init; } = 1;
    public string DebugName { get; init; } = string.Empty;
}

public sealed class GraphicsDevice
{
    private readonly Renderer _renderer;

    internal GraphicsDevice(Renderer renderer) => _renderer = renderer;
    private nint Handle => _renderer.NativeHandle;

    public unsafe ShaderModule CreateShaderModule(ShaderModuleOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        if (string.IsNullOrWhiteSpace(options.SourcePath) && options.Source is null &&
            options.SourceBytes.IsEmpty)
            throw new ArgumentException("A shader needs SourcePath, Source or SourceBytes.", nameof(options));

        using var strings = new Utf8Strings();
        byte[] source = !options.SourceBytes.IsEmpty ? options.SourceBytes.ToArray() :
            options.Source is null ? [] : Encoding.UTF8.GetBytes(options.Source);
        var nativeDefines = MakeDefines(options.Defines, strings);
        fixed (byte* sourcePtr = source)
        fixed (Native.ShaderDefine* definePtr = nativeDefines)
        {
            var desc = new Native.ShaderModuleDesc
            {
                StructSize = (uint)Marshal.SizeOf<Native.ShaderModuleDesc>(),
                Stage = options.Stage,
                Language = options.Language,
                SourcePath = strings.Add(options.SourcePath),
                SourceData = (nint)sourcePtr,
                SourceSize = (nuint)source.Length,
                EntryPoint = strings.Add(options.EntryPoint),
                Defines = (nint)definePtr,
                DefineCount = (nuint)nativeDefines.Length,
                Optimize = options.Optimize ? 1 : 0,
                GenerateDebugInfo = options.GenerateDebugInfo ? 1 : 0,
                EnableHotReload = options.EnableHotReload ? 1 : 0,
                DebugName = strings.Add(options.DebugName)
            };
            return new(Renderer.HandleResult(Native.re_renderer_create_shader_module(Handle, in desc)));
        }
    }

    public unsafe ShaderModule CreateShaderPermutation(ShaderModule shader,
        IReadOnlyList<ShaderDefine> additionalDefines)
    {
        ArgumentNullException.ThrowIfNull(additionalDefines);
        using var strings = new Utf8Strings();
        var defines = MakeDefines(additionalDefines, strings);
        fixed (Native.ShaderDefine* ptr = defines)
            return new(Renderer.HandleResult(Native.re_renderer_create_shader_permutation(
                Handle, shader.Value, (nint)ptr, (nuint)defines.Length)));
    }

    public void DestroyShaderModule(ShaderModule shader) =>
        Native.re_renderer_destroy_shader_module(Handle, shader.Value);

    public unsafe ShaderReflection GetShaderReflection(ShaderModule shader)
    {
        Renderer.Check(Native.re_renderer_get_shader_reflection(Handle, shader.Value, out var info));
        string entryPoint = Utf8(info.EntryPoint);
        string permutation = Utf8(info.PermutationKey);
        var resources = new ShaderResource[info.ResourceCount];
        for (uint i = 0; i < info.ResourceCount; ++i)
        {
            Renderer.Check(Native.re_renderer_get_shader_resource(Handle, shader.Value, i, out var resource));
            string name = Utf8(resource.Name);
            resources[i] = new(name, resource.Type, resource.Set, resource.Binding,
                resource.Location, resource.ArrayCount);
        }
        return new(info.Stage, entryPoint, permutation, info.Generation, resources);
    }

    public unsafe GraphicsPipeline CreateGraphicsPipeline(GraphicsPipelineOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        using var strings = new Utf8Strings();
        var bindings = options.VertexBindings.Select(static value => new Native.VertexBindingDesc
        {
            Binding = value.Binding, Stride = value.Stride, InputRate = value.InputRate
        }).ToArray();
        var attributes = options.VertexAttributes.Select(static value => new Native.VertexAttributeDesc
        {
            Location = value.Location, Binding = value.Binding,
            Format = value.Format, Offset = value.Offset
        }).ToArray();
        TextureFormat[] formats = options.ColorFormats.ToArray();
        fixed (Native.VertexBindingDesc* bindingPtr = bindings)
        fixed (Native.VertexAttributeDesc* attributePtr = attributes)
        fixed (TextureFormat* formatPtr = formats)
        {
            var desc = new Native.GraphicsPipelineDesc
            {
                StructSize = (uint)Marshal.SizeOf<Native.GraphicsPipelineDesc>(),
                VertexShader = options.VertexShader.Value,
                FragmentShader = options.FragmentShader.Value,
                VertexBindings = (nint)bindingPtr,
                VertexBindingCount = (nuint)bindings.Length,
                VertexAttributes = (nint)attributePtr,
                VertexAttributeCount = (nuint)attributes.Length,
                Topology = options.Topology,
                Cull = options.Cull,
                Winding = options.Winding,
                DepthTest = options.DepthTest ? 1 : 0,
                DepthWrite = options.DepthWrite ? 1 : 0,
                DepthCompare = options.DepthCompare,
                BlendEnabled = options.BlendEnabled ? 1 : 0,
                ColorFormats = (nint)formatPtr,
                ColorFormatCount = (nuint)formats.Length,
                HasDepthFormat = options.DepthFormat.HasValue ? 1 : 0,
                DepthFormat = options.DepthFormat ?? TextureFormat.Depth32Float,
                DebugName = strings.Add(options.DebugName)
            };
            return new(Renderer.HandleResult(Native.re_renderer_create_graphics_pipeline(Handle, in desc)));
        }
    }

    public ComputePipeline CreateComputePipeline(ComputePipelineOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        using var strings = new Utf8Strings();
        var desc = new Native.ComputePipelineDesc
        {
            StructSize = (uint)Marshal.SizeOf<Native.ComputePipelineDesc>(),
            ComputeShader = options.ComputeShader.Value,
            DebugName = strings.Add(options.DebugName)
        };
        return new(Renderer.HandleResult(Native.re_renderer_create_compute_pipeline(Handle, in desc)));
    }

    public void DestroyGraphicsPipeline(GraphicsPipeline pipeline) =>
        Native.re_renderer_destroy_graphics_pipeline(Handle, pipeline.Value);
    public void DestroyComputePipeline(ComputePipeline pipeline) =>
        Native.re_renderer_destroy_compute_pipeline(Handle, pipeline.Value);

    public unsafe GpuBuffer CreateBuffer<T>(BufferOptions options, ReadOnlySpan<T> initialData = default)
        where T : unmanaged
    {
        ArgumentNullException.ThrowIfNull(options);
        ReadOnlySpan<byte> bytes = MemoryMarshal.AsBytes(initialData);
        ulong size = options.Size == 0 ? (ulong)bytes.Length : options.Size;
        if (size == 0) throw new ArgumentException("Buffer size must be non-zero.", nameof(options));
        using var strings = new Utf8Strings();
        var desc = new Native.BufferDesc
        {
            StructSize = (uint)Marshal.SizeOf<Native.BufferDesc>(), Size = size,
            Usage = options.Usage, CpuWritable = options.CpuWritable ? 1 : 0,
            DebugName = strings.Add(options.DebugName)
        };
        fixed (byte* ptr = bytes)
            return new(Renderer.HandleResult(Native.re_renderer_create_buffer(
                Handle, in desc, (nint)ptr, (nuint)bytes.Length)));
    }

    public unsafe void UpdateBuffer<T>(GpuBuffer buffer, ulong offset, ReadOnlySpan<T> data)
        where T : unmanaged
    {
        ReadOnlySpan<byte> bytes = MemoryMarshal.AsBytes(data);
        if (bytes.IsEmpty) return;
        fixed (byte* ptr = bytes)
            Renderer.Check(Native.re_renderer_update_buffer(Handle, buffer.Value, offset,
                (nint)ptr, (nuint)bytes.Length));
    }

    public void DestroyBuffer(GpuBuffer buffer) => Native.re_renderer_destroy_buffer(Handle, buffer.Value);

    public unsafe Texture CreateTexture(TextureOptions options, ReadOnlySpan<byte> initialData = default)
    {
        ArgumentNullException.ThrowIfNull(options);
        using var strings = new Utf8Strings();
        var desc = new Native.TextureDesc
        {
            StructSize = (uint)Marshal.SizeOf<Native.TextureDesc>(), Width = options.Width,
            Height = options.Height, MipLevels = options.MipLevels, Format = options.Format,
            Usage = options.Usage, DebugName = strings.Add(options.DebugName)
        };
        fixed (byte* ptr = initialData)
            return new(Renderer.HandleResult(Native.re_renderer_create_texture(
                Handle, in desc, (nint)ptr, (nuint)initialData.Length)));
    }

    public unsafe void UpdateTexture(Texture texture, uint mipLevel, ReadOnlySpan<byte> data)
    {
        if (data.IsEmpty) return;
        fixed (byte* ptr = data)
            Renderer.Check(Native.re_renderer_update_texture(Handle, texture.Value, mipLevel,
                (nint)ptr, (nuint)data.Length));
    }

    public void DestroyTexture(Texture texture) => Native.re_renderer_destroy_texture(Handle, texture.Value);

    public Sampler CreateSampler(SamplerOptions? options = null)
    {
        options ??= new SamplerOptions();
        using var strings = new Utf8Strings();
        var desc = new Native.SamplerDesc
        {
            StructSize = (uint)Marshal.SizeOf<Native.SamplerDesc>(),
            MinFilter = options.MinFilter, MagFilter = options.MagFilter,
            AddressU = options.AddressU, AddressV = options.AddressV, AddressW = options.AddressW,
            MaxAnisotropy = options.MaxAnisotropy, DebugName = strings.Add(options.DebugName)
        };
        return new(Renderer.HandleResult(Native.re_renderer_create_sampler(Handle, in desc)));
    }

    public void DestroySampler(Sampler sampler) => Native.re_renderer_destroy_sampler(Handle, sampler.Value);

    public unsafe RenderTarget CreateRenderTarget(RenderTargetOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        using var strings = new Utf8Strings();
        ulong[] colors = options.ColorAttachments.Select(static texture => texture.Value).ToArray();
        fixed (ulong* colorPtr = colors)
        {
            var desc = new Native.RenderTargetDesc
            {
                StructSize = (uint)Marshal.SizeOf<Native.RenderTargetDesc>(),
                ColorAttachments = (nint)colorPtr,
                ColorAttachmentCount = (nuint)colors.Length,
                DepthAttachment = options.DepthAttachment.Value,
                DebugName = strings.Add(options.DebugName)
            };
            return new(Renderer.HandleResult(Native.re_renderer_create_render_target(Handle, in desc)));
        }
    }

    public void DestroyRenderTarget(RenderTarget target) =>
        Native.re_renderer_destroy_render_target(Handle, target.Value);

    public unsafe void BeginRenderPass(RenderPassOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        if (options.ClearColor.Length != 4)
            throw new ArgumentException("ClearColor must contain four values.", nameof(options));
        using var strings = new Utf8Strings();
        var desc = new Native.RenderPassDesc
        {
            StructSize = (uint)Marshal.SizeOf<Native.RenderPassDesc>(), Target = options.Target.Value,
            ColorLoad = options.ColorLoad, ColorStore = options.ColorStore,
            DepthLoad = options.DepthLoad, DepthStore = options.DepthStore,
            ClearDepth = options.ClearDepth, DebugName = strings.Add(options.DebugName)
        };
        for (int i = 0; i < 4; ++i) desc.ClearColor[i] = options.ClearColor[i];
        Renderer.Check(Native.re_renderer_begin_render_pass(Handle, in desc));
    }

    public void EndRenderPass() => Renderer.Check(Native.re_renderer_end_render_pass(Handle));
    public void BindGraphicsPipeline(GraphicsPipeline pipeline) =>
        Renderer.Check(Native.re_renderer_bind_graphics_pipeline(Handle, pipeline.Value));
    public void BindComputePipeline(ComputePipeline pipeline) =>
        Renderer.Check(Native.re_renderer_bind_compute_pipeline(Handle, pipeline.Value));
    public void BindVertexBuffer(uint binding, GpuBuffer buffer, ulong offset = 0) =>
        Renderer.Check(Native.re_renderer_bind_vertex_buffer(Handle, binding, buffer.Value, offset));
    public void BindIndexBuffer(GpuBuffer buffer, IndexType type = IndexType.UInt32, ulong offset = 0) =>
        Renderer.Check(Native.re_renderer_bind_index_buffer(Handle, buffer.Value, type, offset));
    public void BindUniformBuffer(uint set, uint binding, GpuBuffer buffer,
        ulong offset = 0, ulong size = 0) => Renderer.Check(Native.re_renderer_bind_uniform_buffer(
            Handle, set, binding, buffer.Value, offset, size));
    public void BindStorageBuffer(uint set, uint binding, GpuBuffer buffer,
        ulong offset = 0, ulong size = 0) => Renderer.Check(Native.re_renderer_bind_storage_buffer(
            Handle, set, binding, buffer.Value, offset, size));
    public void BindTexture(uint set, uint binding, Texture texture, Sampler sampler = default) =>
        Renderer.Check(Native.re_renderer_bind_texture(Handle, set, binding, texture.Value, sampler.Value));
    public void BindSampler(uint set, uint binding, Sampler sampler) =>
        Renderer.Check(Native.re_renderer_bind_sampler(Handle, set, binding, sampler.Value));
    public void BindStorageTexture(uint set, uint binding, Texture texture) =>
        Renderer.Check(Native.re_renderer_bind_storage_texture(Handle, set, binding, texture.Value));
    public void Draw(uint vertexCount, uint instanceCount = 1,
        uint firstVertex = 0, uint firstInstance = 0) => Renderer.Check(
            Native.re_renderer_draw(Handle, vertexCount, instanceCount, firstVertex, firstInstance));
    public void DrawIndexed(uint indexCount, uint instanceCount = 1, uint firstIndex = 0,
        int vertexOffset = 0, uint firstInstance = 0) => Renderer.Check(
            Native.re_renderer_draw_indexed(Handle, indexCount, instanceCount,
                firstIndex, vertexOffset, firstInstance));
    public void Dispatch(uint x, uint y = 1, uint z = 1) =>
        Renderer.Check(Native.re_renderer_dispatch(Handle, x, y, z));
    public void ResetCommands() => Native.re_renderer_reset_graphics_commands(Handle);

    public uint ReloadChangedShaders() => Native.re_renderer_reload_changed_shaders(Handle);
    public string LastShaderError => Marshal.PtrToStringUTF8(Native.re_get_last_error()) ?? string.Empty;

    private static Native.ShaderDefine[] MakeDefines(IReadOnlyList<ShaderDefine> values,
        Utf8Strings strings)
    {
        var result = new Native.ShaderDefine[values.Count];
        for (int i = 0; i < values.Count; ++i)
        {
            if (string.IsNullOrWhiteSpace(values[i].Name))
                throw new ArgumentException("Shader define names cannot be empty.", nameof(values));
            result[i] = new Native.ShaderDefine
            {
                Name = strings.Add(values[i].Name), Value = strings.Add(values[i].Value)
            };
        }
        return result;
    }

    private static unsafe string Utf8(byte* value) =>
        Marshal.PtrToStringUTF8((nint)value) ?? string.Empty;

    private sealed class Utf8Strings : IDisposable
    {
        private readonly List<nint> _values = [];
        public nint Add(string? value)
        {
            if (string.IsNullOrEmpty(value)) return 0;
            nint pointer = Marshal.StringToCoTaskMemUTF8(value);
            _values.Add(pointer);
            return pointer;
        }
        public void Dispose()
        {
            foreach (nint value in _values) Marshal.FreeCoTaskMem(value);
        }
    }
}
