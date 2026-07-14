#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::debug { struct FrameDebugSnapshot; }

namespace engine::rendergraph
{

constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

struct ResourceHandle
{
    uint32_t Index = kInvalidIndex;
    bool Valid() const { return Index != kInvalidIndex; }
    friend bool operator==(ResourceHandle, ResourceHandle) = default;
};

enum class ResourceType : uint8_t
{
    Texture,
    Buffer
};

enum class ResourceState : uint8_t
{
    Undefined,
    ShaderRead,
    ShaderWrite,
    ColorAttachment,
    DepthWrite,
    DepthRead,
    TransferSource,
    TransferDestination,
    Present
};

enum class PipelineStage : uint8_t
{
    None,
    Vertex,
    Fragment,
    EarlyDepth,
    LateDepth,
    ColorOutput,
    Compute,
    AllShaders,
    Transfer,
    Present
};

enum class PassPhase : uint8_t
{
    Prepare,
    Render
};

struct ResourceDesc
{
    std::string Name;
    ResourceType Type = ResourceType::Texture;
    std::string Format = "Unknown";
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t DepthOrLayers = 1;
    uint32_t MipLevels = 1;
    uint32_t Samples = 1;
    uint32_t BytesPerPixel = 0;
    uint64_t ExplicitBytes = 0;
    bool Transient = true;
    bool AllowAliasing = true;
    ResourceState InitialState = ResourceState::Undefined;
    PipelineStage InitialStage = PipelineStage::None;

    uint64_t EstimatedBytes() const;
    bool AliasCompatible(const ResourceDesc& other) const;
};

struct ResourceAccess
{
    ResourceHandle Resource;
    ResourceState State = ResourceState::ShaderRead;
    PipelineStage Stage = PipelineStage::Fragment;
};

struct PassOverride
{
    std::string Id;
    std::optional<bool> Enabled;
    std::vector<std::string> After;
};

struct Config
{
    bool Enabled = true;
    bool Validation = true;
    bool TransientAliasing = true;
    std::vector<PassOverride> Passes;
};

struct Barrier
{
    ResourceHandle Resource;
    ResourceState Before = ResourceState::Undefined;
    ResourceState After = ResourceState::Undefined;
    PipelineStage SourceStage = PipelineStage::None;
    PipelineStage DestinationStage = PipelineStage::None;
    bool WriteHazard = false;
};

struct CompiledResource
{
    ResourceHandle Handle;
    ResourceDesc Description;
    uint32_t FirstUsePass = kInvalidIndex;
    uint32_t LastUsePass = kInvalidIndex;
    uint32_t AliasSlot = kInvalidIndex;
};

struct CompiledPass
{
    uint32_t DeclarationIndex = 0;
    std::string Id;
    std::string Name;
    PassPhase Phase = PassPhase::Render;
    std::vector<ResourceHandle> Inputs;
    std::vector<ResourceHandle> Outputs;
    std::vector<Barrier> Barriers;
};

struct Statistics
{
    uint32_t DeclaredPasses = 0;
    uint32_t CompiledPasses = 0;
    uint32_t BarrierCount = 0;
    uint32_t AliasSlotCount = 0;
    uint32_t AliasedResourceCount = 0;
    uint64_t LogicalTransientBytes = 0;
    uint64_t PhysicalTransientBytes = 0;
    uint64_t AliasedBytesSaved = 0;
    double CompileMilliseconds = 0.0;
    double ExecuteMilliseconds = 0.0;
};

class RenderGraph;

class PassBuilder
{
  public:
    PassBuilder& Read(ResourceHandle resource,
                      ResourceState state = ResourceState::ShaderRead,
                      PipelineStage stage = PipelineStage::Fragment);
    PassBuilder& Write(ResourceHandle resource, ResourceState state,
                       PipelineStage stage);
    PassBuilder& After(std::string passId);
    PassBuilder& SetEnabled(bool enabled);
    PassBuilder& SetSideEffect(bool sideEffect = true);
    PassBuilder& SetPhase(PassPhase phase);
    PassBuilder& SetExecute(std::function<void()> execute);

  private:
    friend class RenderGraph;
    PassBuilder(RenderGraph& graph, uint32_t passIndex)
        : m_graph(graph), m_passIndex(passIndex) {}
    RenderGraph& m_graph;
    uint32_t m_passIndex;
};

class RenderGraph
{
  public:
    using PassHook = std::function<void(const CompiledPass&)>;

    RenderGraph();
    ~RenderGraph();
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    void Reset(Config config = {});
    ResourceHandle Create(ResourceDesc description);
    ResourceHandle Import(ResourceDesc description);
    PassBuilder AddPass(std::string id, std::string displayName);
    bool Compile(std::string* error = nullptr);
    bool SetPassExecute(std::string_view id, std::function<void()> execute);
    void ExecutePhase(PassPhase phase, const PassHook& beforePass = {},
                      const PassHook& afterPass = {});

    const Config& GetConfig() const
    {
        return m_config;
    }
    const std::vector<CompiledPass>& Passes() const
    {
        return m_compiledPasses;
    }
    const std::vector<CompiledResource>& Resources() const
    {
        return m_compiledResources;
    }
    const Statistics& Stats() const
    {
        return m_statistics;
    }
    const ResourceDesc* FindResource(ResourceHandle handle) const;
    const ResourceDesc* FindResource(std::string_view name) const;
    const CompiledPass* FindPass(std::string_view id) const;
    void PopulateFrameDebugSnapshot(debug::FrameDebugSnapshot& snapshot) const;

  private:
    friend class PassBuilder;
    struct Pass;
    Pass& MutablePass(uint32_t index);
    bool Fail(std::string message, std::string* error);
    void BuildDependencies(std::vector<std::vector<uint32_t>>& edges,
                           std::vector<uint32_t>& indegree,
                           const std::vector<uint32_t>& active) const;
    void ComputeLifetimesAndAliasing();
    void ComputeBarriers();

    Config m_config;
    std::vector<ResourceDesc> m_resources;
    std::vector<Pass> m_passes;
    std::vector<CompiledPass> m_compiledPasses;
    std::vector<CompiledResource> m_compiledResources;
    Statistics m_statistics;
    bool m_compiled = false;
};

bool LoadConfig(const std::filesystem::path& path, Config& config,
                std::string* error = nullptr);
bool SaveConfig(const std::filesystem::path& path, const Config& config,
                std::string* error = nullptr);
const char* ResourceStateName(ResourceState state);
const char* PipelineStageName(PipelineStage stage);

} // namespace engine::rendergraph
