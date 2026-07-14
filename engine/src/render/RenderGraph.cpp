#include "engine/render/RenderGraph.h"

#include "engine/debug/FrameDebugger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace engine::rendergraph
{
namespace
{

bool IsWrite(ResourceState state)
{
    return state == ResourceState::ShaderWrite ||
           state == ResourceState::ColorAttachment ||
           state == ResourceState::DepthWrite ||
           state == ResourceState::TransferDestination;
}

std::string Trim(std::string value)
{
    const auto whitespace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(),
                std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(),
                value.end());
    return value;
}

std::vector<std::string> Split(std::string_view value)
{
    std::vector<std::string> result;
    size_t begin = 0;
    while (begin <= value.size())
    {
        const size_t end = value.find(',', begin);
        std::string item = Trim(std::string(
            value.substr(begin, end == std::string_view::npos ? value.size() - begin
                                                              : end - begin)));
        if (!item.empty())
            result.push_back(std::move(item));
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return result;
}

bool ParseBool(std::string value, bool& result)
{
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "true" || value == "1" || value == "on")
    {
        result = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "off")
    {
        result = false;
        return true;
    }
    return false;
}

uint64_t TextureBytes(const ResourceDesc& desc)
{
    uint64_t result = 0;
    uint32_t width = std::max(desc.Width, 1u);
    uint32_t height = std::max(desc.Height, 1u);
    for (uint32_t mip = 0; mip < std::max(desc.MipLevels, 1u); ++mip)
    {
        result += static_cast<uint64_t>(width) * height *
                  std::max(desc.DepthOrLayers, 1u) * std::max(desc.Samples, 1u) *
                  desc.BytesPerPixel;
        width = std::max(width / 2, 1u);
        height = std::max(height / 2, 1u);
    }
    return result;
}

} // namespace

struct RenderGraph::Pass
{
    std::string Id;
    std::string Name;
    bool Enabled = true;
    bool SideEffect = false;
    PassPhase Phase = PassPhase::Render;
    std::vector<ResourceAccess> Reads;
    std::vector<ResourceAccess> Writes;
    std::vector<std::string> After;
    std::function<void()> Execute;
};

RenderGraph::RenderGraph() = default;
RenderGraph::~RenderGraph() = default;

uint64_t ResourceDesc::EstimatedBytes() const
{
    if (ExplicitBytes > 0)
        return ExplicitBytes;
    if (Type == ResourceType::Buffer)
        return static_cast<uint64_t>(Width);
    return TextureBytes(*this);
}

bool ResourceDesc::AliasCompatible(const ResourceDesc& other) const
{
    return Type == other.Type && Format == other.Format && Width == other.Width &&
           Height == other.Height && DepthOrLayers == other.DepthOrLayers &&
           MipLevels == other.MipLevels && Samples == other.Samples &&
           BytesPerPixel == other.BytesPerPixel &&
           ExplicitBytes == other.ExplicitBytes;
}

RenderGraph::Pass& RenderGraph::MutablePass(uint32_t index)
{
    if (index >= m_passes.size())
        throw std::out_of_range("Invalid render-graph pass index");
    return m_passes[index];
}

PassBuilder& PassBuilder::Read(ResourceHandle resource, ResourceState state,
                               PipelineStage stage)
{
    m_graph.MutablePass(m_passIndex).Reads.push_back({resource, state, stage});
    return *this;
}

PassBuilder& PassBuilder::Write(ResourceHandle resource, ResourceState state,
                                PipelineStage stage)
{
    m_graph.MutablePass(m_passIndex).Writes.push_back({resource, state, stage});
    return *this;
}

PassBuilder& PassBuilder::After(std::string passId)
{
    m_graph.MutablePass(m_passIndex).After.push_back(std::move(passId));
    return *this;
}

PassBuilder& PassBuilder::SetEnabled(bool enabled)
{
    m_graph.MutablePass(m_passIndex).Enabled = enabled;
    return *this;
}

PassBuilder& PassBuilder::SetSideEffect(bool sideEffect)
{
    m_graph.MutablePass(m_passIndex).SideEffect = sideEffect;
    return *this;
}

PassBuilder& PassBuilder::SetPhase(PassPhase phase)
{
    m_graph.MutablePass(m_passIndex).Phase = phase;
    return *this;
}

PassBuilder& PassBuilder::SetExecute(std::function<void()> execute)
{
    m_graph.MutablePass(m_passIndex).Execute = std::move(execute);
    return *this;
}

void RenderGraph::Reset(Config config)
{
    m_config = std::move(config);
    m_resources.clear();
    m_passes.clear();
    m_compiledPasses.clear();
    m_compiledResources.clear();
    m_statistics = {};
    m_compiled = false;
}

ResourceHandle RenderGraph::Create(ResourceDesc description)
{
    if (m_compiled)
        throw std::logic_error(
            "Cannot create a render-graph resource after compilation");
    description.Transient = true;
    const ResourceHandle handle{static_cast<uint32_t>(m_resources.size())};
    m_resources.push_back(std::move(description));
    return handle;
}

ResourceHandle RenderGraph::Import(ResourceDesc description)
{
    if (m_compiled)
        throw std::logic_error(
            "Cannot import a render-graph resource after compilation");
    description.Transient = false;
    description.AllowAliasing = false;
    const ResourceHandle handle{static_cast<uint32_t>(m_resources.size())};
    m_resources.push_back(std::move(description));
    return handle;
}

PassBuilder RenderGraph::AddPass(std::string id, std::string displayName)
{
    if (m_compiled)
        throw std::logic_error("Cannot add a render-graph pass after compilation");
    Pass pass;
    pass.Id = std::move(id);
    pass.Name = std::move(displayName);
    const uint32_t index = static_cast<uint32_t>(m_passes.size());
    m_passes.push_back(std::move(pass));
    return PassBuilder(*this, index);
}

bool RenderGraph::Fail(std::string message, std::string* error)
{
    if (error)
        *error = std::move(message);
    return false;
}

void RenderGraph::BuildDependencies(std::vector<std::vector<uint32_t>>& edges,
                                    std::vector<uint32_t>& indegree,
                                    const std::vector<uint32_t>& active) const
{
    std::unordered_map<std::string, uint32_t> ids;
    for (uint32_t index = 0; index < active.size(); ++index)
        ids.emplace(m_passes[active[index]].Id, index);
    std::unordered_set<uint64_t> inserted;
    const auto addEdge = [&](uint32_t from, uint32_t to) {
        if (from == to)
            return;
        const uint64_t key = (static_cast<uint64_t>(from) << 32u) | to;
        if (inserted.insert(key).second)
        {
            edges[from].push_back(to);
            ++indegree[to];
        }
    };
    for (uint32_t activePass = 0; activePass < active.size(); ++activePass)
    {
        const Pass& pass = m_passes[active[activePass]];
        for (const std::string& dependency : pass.After)
        {
            const auto found = ids.find(dependency);
            if (found != ids.end())
                addEdge(found->second, activePass);
        }
    }

    // Build hazards per resource instead of walking only in declaration order.
    // This lets a consumer be declared before its unique transient producer,
    // which is important for data-driven pipeline assets.
    for (uint32_t resourceIndex = 0; resourceIndex < m_resources.size();
         ++resourceIndex)
    {
        std::vector<uint32_t> writers;
        std::vector<uint32_t> readers;
        for (uint32_t activePass = 0; activePass < active.size(); ++activePass)
        {
            const Pass& pass = m_passes[active[activePass]];
            if (std::any_of(pass.Writes.begin(), pass.Writes.end(),
                            [resourceIndex](const ResourceAccess& access) {
                                return access.Resource.Index == resourceIndex;
                            }))
                writers.push_back(activePass);
            if (std::any_of(pass.Reads.begin(), pass.Reads.end(),
                            [resourceIndex](const ResourceAccess& access) {
                                return access.Resource.Index == resourceIndex;
                            }))
                readers.push_back(activePass);
        }
        for (size_t index = 1; index < writers.size(); ++index)
            addEdge(writers[index - 1], writers[index]);

        for (uint32_t reader : readers)
        {
            uint32_t producer = kInvalidIndex;
            for (uint32_t writer : writers)
            {
                if (writer < reader)
                    producer = writer;
                else
                    break;
            }
            if (producer == kInvalidIndex && m_resources[resourceIndex].Transient &&
                !writers.empty())
                producer = writers.front();
            if (producer != kInvalidIndex)
                addEdge(producer, reader);

            const auto next =
                std::find_if(writers.begin(), writers.end(),
                             [reader](uint32_t writer) { return writer > reader; });
            if (next != writers.end() && *next != producer)
                addEdge(reader, *next);
        }
    }
}

bool RenderGraph::Compile(std::string* error)
{
    const auto begin = std::chrono::steady_clock::now();
    m_compiledPasses.clear();
    m_compiledResources.clear();
    m_statistics = {};
    m_statistics.DeclaredPasses = static_cast<uint32_t>(m_passes.size());
    std::unordered_set<std::string> passIds;
    for (Pass& pass : m_passes)
    {
        for (const PassOverride& override : m_config.Passes)
        {
            if (override.Id != pass.Id)
                continue;
            if (override.Enabled)
                pass.Enabled = *override.Enabled;
            pass.After.insert(pass.After.end(), override.After.begin(),
                              override.After.end());
        }
    }
    for (uint32_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
    {
        const Pass& pass = m_passes[passIndex];
        if (pass.Id.empty() || !passIds.insert(pass.Id).second)
            return Fail("Duplicate or empty render-graph pass id: " + pass.Id, error);
        if (pass.Enabled && !pass.Execute)
            return Fail("Render-graph pass has no execute callback: " + pass.Id,
                        error);
        for (const ResourceAccess& access : pass.Reads)
            if (!access.Resource.Valid() ||
                access.Resource.Index >= m_resources.size())
                return Fail("Invalid read resource in pass: " + pass.Id, error);
        for (const ResourceAccess& access : pass.Writes)
            if (!access.Resource.Valid() ||
                access.Resource.Index >= m_resources.size())
                return Fail("Invalid write resource in pass: " + pass.Id, error);
    }
    for (const PassOverride& override : m_config.Passes)
        if (!passIds.contains(override.Id))
            return Fail("Render-graph config references unknown pass: " + override.Id,
                        error);

    std::vector<uint32_t> active;
    for (uint32_t index = 0; index < m_passes.size(); ++index)
        if (m_passes[index].Enabled)
            active.push_back(index);

    for (uint32_t index : active)
        for (const std::string& dependency : m_passes[index].After)
            if (!passIds.contains(dependency))
                return Fail("Render-graph pass '" + m_passes[index].Id +
                                "' depends on unknown pass '" + dependency + "'",
                            error);

    std::vector<uint32_t> order;
    if (m_config.Enabled)
    {
        std::vector<std::vector<uint32_t>> edges(active.size());
        std::vector<uint32_t> indegree(active.size(), 0);
        BuildDependencies(edges, indegree, active);
        std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<>> ready;
        for (uint32_t index = 0; index < active.size(); ++index)
            if (indegree[index] == 0)
                ready.push(index);
        while (!ready.empty())
        {
            const uint32_t index = ready.top();
            ready.pop();
            order.push_back(active[index]);
            for (uint32_t output : edges[index])
                if (--indegree[output] == 0)
                    ready.push(output);
        }
        if (order.size() != active.size())
            return Fail("Render-graph dependency cycle detected", error);
    }
    else
        order = active;

    bool renderPhaseSeen = false;
    for (uint32_t declarationIndex : order)
    {
        if (m_passes[declarationIndex].Phase == PassPhase::Render)
            renderPhaseSeen = true;
        else if (renderPhaseSeen)
            return Fail("Render-graph prepare pass '" + m_passes[declarationIndex].Id +
                        "' depends on a render-phase pass", error);
    }

    std::vector<bool> hasWriter(m_resources.size(), false);
    for (uint32_t declarationIndex : order)
    {
        const Pass& pass = m_passes[declarationIndex];
        CompiledPass compiled;
        compiled.DeclarationIndex = declarationIndex;
        compiled.Id = pass.Id;
        compiled.Name = pass.Name;
        compiled.Phase = pass.Phase;
        for (const ResourceAccess& read : pass.Reads)
        {
            if (m_config.Enabled && m_config.Validation &&
                m_resources[read.Resource.Index].Transient &&
                !hasWriter[read.Resource.Index])
                return Fail(
                    "Transient resource '" + m_resources[read.Resource.Index].Name +
                        "' is read before it is written by pass '" + pass.Id + "'",
                    error);
            compiled.Inputs.push_back(read.Resource);
        }
        for (const ResourceAccess& write : pass.Writes)
        {
            hasWriter[write.Resource.Index] = true;
            compiled.Outputs.push_back(write.Resource);
        }
        m_compiledPasses.push_back(std::move(compiled));
    }
    m_statistics.CompiledPasses = static_cast<uint32_t>(m_compiledPasses.size());
    ComputeLifetimesAndAliasing();
    ComputeBarriers();
    m_statistics.CompileMilliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin)
            .count();
    m_compiled = true;
    if (error)
        error->clear();
    return true;
}

bool RenderGraph::SetPassExecute(std::string_view id,
                                 std::function<void()> execute)
{
    const auto found =
        std::find_if(m_passes.begin(), m_passes.end(),
                     [id](const Pass& pass) { return pass.Id == id; });
    if (found == m_passes.end())
        return false;
    found->Execute = std::move(execute);
    return true;
}

void RenderGraph::ComputeLifetimesAndAliasing()
{
    m_compiledResources.reserve(m_resources.size());
    for (uint32_t index = 0; index < m_resources.size(); ++index)
        m_compiledResources.push_back({ResourceHandle{index}, m_resources[index]});
    for (uint32_t passIndex = 0; passIndex < m_compiledPasses.size();
         ++passIndex)
    {
        const CompiledPass& pass = m_compiledPasses[passIndex];
        for (const ResourceHandle handle : pass.Inputs)
        {
            CompiledResource& resource = m_compiledResources[handle.Index];
            resource.FirstUsePass = std::min(resource.FirstUsePass, passIndex);
            resource.LastUsePass = passIndex;
        }
        for (const ResourceHandle handle : pass.Outputs)
        {
            CompiledResource& resource = m_compiledResources[handle.Index];
            resource.FirstUsePass = std::min(resource.FirstUsePass, passIndex);
            resource.LastUsePass = passIndex;
        }
    }

    struct Slot
    {
        ResourceDesc Desc;
        uint32_t LastUse = 0;
        uint64_t Bytes = 0;
    };
    std::vector<Slot> slots;
    std::vector<uint32_t> resources;
    for (uint32_t index = 0; index < m_compiledResources.size(); ++index)
    {
        const CompiledResource& resource = m_compiledResources[index];
        if (resource.Description.Transient &&
            resource.FirstUsePass != kInvalidIndex)
            resources.push_back(index);
    }
    std::sort(resources.begin(), resources.end(),
              [&](uint32_t left, uint32_t right) {
                  return m_compiledResources[left].FirstUsePass <
                         m_compiledResources[right].FirstUsePass;
              });
    for (uint32_t index : resources)
    {
        CompiledResource& resource = m_compiledResources[index];
        const uint64_t bytes = resource.Description.EstimatedBytes();
        m_statistics.LogicalTransientBytes += bytes;
        uint32_t slotIndex = kInvalidIndex;
        if (m_config.Enabled && m_config.TransientAliasing &&
            resource.Description.AllowAliasing)
        {
            for (uint32_t candidate = 0; candidate < slots.size(); ++candidate)
            {
                if (slots[candidate].LastUse < resource.FirstUsePass &&
                    slots[candidate].Desc.AliasCompatible(resource.Description))
                {
                    slotIndex = candidate;
                    break;
                }
            }
        }
        if (slotIndex == kInvalidIndex)
        {
            slotIndex = static_cast<uint32_t>(slots.size());
            slots.push_back({resource.Description, resource.LastUsePass, bytes});
            m_statistics.PhysicalTransientBytes += bytes;
        }
        else
        {
            slots[slotIndex].LastUse = resource.LastUsePass;
            ++m_statistics.AliasedResourceCount;
        }
        resource.AliasSlot = slotIndex;
    }
    m_statistics.AliasSlotCount = static_cast<uint32_t>(slots.size());
    m_statistics.AliasedBytesSaved =
        m_statistics.LogicalTransientBytes - m_statistics.PhysicalTransientBytes;
}

void RenderGraph::ComputeBarriers()
{
    struct State
    {
        ResourceState Access;
        PipelineStage Stage;
        bool Written = false;
    };
    std::vector<State> states(m_resources.size());
    for (uint32_t index = 0; index < m_resources.size(); ++index)
        states[index] = {m_resources[index].InitialState,
                         m_resources[index].InitialStage, false};
    for (CompiledPass& compiled : m_compiledPasses)
    {
        const Pass& pass = m_passes[compiled.DeclarationIndex];
        const auto process = [&](const ResourceAccess& access) {
            State& previous = states[access.Resource.Index];
            const bool writeHazard = previous.Written || IsWrite(access.State);
            if (previous.Access != access.State || previous.Stage != access.Stage ||
                writeHazard)
            {
                compiled.Barriers.push_back({access.Resource, previous.Access,
                                             access.State, previous.Stage, access.Stage,
                                             writeHazard});
                ++m_statistics.BarrierCount;
            }
            previous = {access.State, access.Stage, IsWrite(access.State)};
        };
        for (const ResourceAccess& read : pass.Reads)
            process(read);
        for (const ResourceAccess& write : pass.Writes)
            process(write);
    }
}

void RenderGraph::ExecutePhase(PassPhase phase, const PassHook& beforePass,
                               const PassHook& afterPass)
{
    if (!m_compiled)
        throw std::logic_error("Render graph must be compiled before execution");
    const auto begin = std::chrono::steady_clock::now();
    for (const CompiledPass& compiled : m_compiledPasses)
    {
        if (compiled.Phase != phase)
            continue;
        if (beforePass)
            beforePass(compiled);
        m_passes[compiled.DeclarationIndex].Execute();
        if (afterPass)
            afterPass(compiled);
    }
    m_statistics.ExecuteMilliseconds +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin)
            .count();
}

const ResourceDesc* RenderGraph::FindResource(ResourceHandle handle) const
{
    return handle.Valid() && handle.Index < m_resources.size()
               ? &m_resources[handle.Index]
               : nullptr;
}

const ResourceDesc* RenderGraph::FindResource(std::string_view name) const
{
    const auto found = std::find_if(
        m_resources.begin(), m_resources.end(),
        [name](const ResourceDesc& resource) { return resource.Name == name; });
    return found == m_resources.end() ? nullptr : &*found;
}

const CompiledPass* RenderGraph::FindPass(std::string_view id) const
{
    const auto found =
        std::find_if(m_compiledPasses.begin(), m_compiledPasses.end(),
                     [id](const CompiledPass& pass) { return pass.Id == id; });
    return found == m_compiledPasses.end() ? nullptr : &*found;
}

void RenderGraph::PopulateFrameDebugSnapshot(
    debug::FrameDebugSnapshot& snapshot) const
{
    snapshot.Passes.clear();
    for (const CompiledPass& pass : m_compiledPasses)
    {
        debug::FrameDebugPass debugPass;
        debugPass.Name = pass.Name;
        debugPass.BarrierCount = static_cast<uint32_t>(pass.Barriers.size());
        debugPass.DeclarationIndex = pass.DeclarationIndex;
        for (ResourceHandle input : pass.Inputs)
            debugPass.Inputs.push_back(
                debug::FrameDebugId(m_resources[input.Index].Name));
        for (ResourceHandle output : pass.Outputs)
            debugPass.Outputs.push_back(
                debug::FrameDebugId(m_resources[output.Index].Name));
        snapshot.Passes.push_back(std::move(debugPass));
    }
    for (const CompiledResource& graphResource : m_compiledResources)
    {
        const uint64_t id = debug::FrameDebugId(graphResource.Description.Name);
        const auto found =
            std::find_if(snapshot.Resources.begin(), snapshot.Resources.end(),
                         [id](const debug::FrameDebugResource& resource) {
                             return resource.Id == id;
                         });
        if (found == snapshot.Resources.end())
            continue;
        found->Transient = graphResource.Description.Transient;
        found->FirstUsePass = graphResource.FirstUsePass;
        found->LastUsePass = graphResource.LastUsePass;
        found->AliasSlot = graphResource.AliasSlot;
    }
    snapshot.GraphBarrierCount = m_statistics.BarrierCount;
    snapshot.GraphLogicalTransientBytes = m_statistics.LogicalTransientBytes;
    snapshot.GraphPhysicalTransientBytes = m_statistics.PhysicalTransientBytes;
    snapshot.GraphAliasedBytesSaved = m_statistics.AliasedBytesSaved;
    snapshot.GraphCompileMilliseconds =
        static_cast<float>(m_statistics.CompileMilliseconds);
}

bool LoadConfig(const std::filesystem::path& path, Config& config,
                std::string* error)
{
    std::ifstream file(path);
    if (!file)
    {
        if (error)
            *error = "Cannot open render-graph config: " + path.string();
        return false;
    }
    Config loaded;
    std::unordered_map<std::string, size_t> passIndices;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;
        line = Trim(line);
        if (line.empty() || line.front() == '#')
            continue;
        const size_t separator = line.find('=');
        if (separator == std::string::npos)
        {
            if (error)
                *error =
                    "Invalid render-graph config line " + std::to_string(lineNumber);
            return false;
        }
        const std::string key = Trim(line.substr(0, separator));
        const std::string value = Trim(line.substr(separator + 1));
        if (key == "version")
        {
            if (value != "1")
            {
                if (error)
                    *error = "Unsupported render-graph config version: " + value;
                return false;
            }
            continue;
        }
        if (key == "enabled" || key == "validation" ||
            key == "transient_aliasing")
        {
            bool parsed = false;
            if (!ParseBool(value, parsed))
            {
                if (error)
                    *error = "Invalid boolean for '" + key + "'";
                return false;
            }
            if (key == "enabled")
                loaded.Enabled = parsed;
            else if (key == "validation")
                loaded.Validation = parsed;
            else
                loaded.TransientAliasing = parsed;
            continue;
        }
        if (key.rfind("pass.", 0) != 0)
        {
            if (error)
                *error = "Unknown render-graph config key: " + key;
            return false;
        }
        const size_t propertySeparator = key.find('.', 5);
        if (propertySeparator == std::string::npos)
        {
            if (error)
                *error = "Invalid pass override key: " + key;
            return false;
        }
        const std::string id = key.substr(5, propertySeparator - 5);
        const std::string property = key.substr(propertySeparator + 1);
        size_t index;
        if (const auto found = passIndices.find(id); found != passIndices.end())
            index = found->second;
        else
        {
            index = loaded.Passes.size();
            passIndices.emplace(id, index);
            loaded.Passes.push_back({id});
        }
        if (property == "enabled")
        {
            bool parsed = false;
            if (!ParseBool(value, parsed))
            {
                if (error)
                    *error = "Invalid pass enabled value for: " + id;
                return false;
            }
            loaded.Passes[index].Enabled = parsed;
        }
        else if (property == "after")
            loaded.Passes[index].After = Split(value);
        else
        {
            if (error)
                *error = "Unknown pass override property: " + property;
            return false;
        }
    }
    config = std::move(loaded);
    if (error)
        error->clear();
    return true;
}

bool SaveConfig(const std::filesystem::path& path, const Config& config,
                std::string* error)
{
    std::error_code directoryError;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), directoryError);
    std::ofstream file(path, std::ios::trunc);
    if (directoryError || !file)
    {
        if (error)
            *error = directoryError
                         ? directoryError.message()
                         : "Cannot write render-graph config: " + path.string();
        return false;
    }
    file << "# Source-Like Renderer graph configuration v1\nversion=1\n"
         << std::boolalpha << "enabled=" << config.Enabled << "\n"
         << "validation=" << config.Validation << "\n"
         << "transient_aliasing=" << config.TransientAliasing << "\n";
    for (const PassOverride& pass : config.Passes)
    {
        if (pass.Enabled)
            file << "pass." << pass.Id << ".enabled=" << *pass.Enabled << "\n";
        if (!pass.After.empty())
        {
            file << "pass." << pass.Id << ".after=";
            for (size_t index = 0; index < pass.After.size(); ++index)
                file << (index ? "," : "") << pass.After[index];
            file << "\n";
        }
    }
    if (error)
        error->clear();
    return static_cast<bool>(file);
}

const char* ResourceStateName(ResourceState state)
{
    switch (state)
    {
    case ResourceState::ShaderRead:
        return "shader-read";
    case ResourceState::ShaderWrite:
        return "shader-write";
    case ResourceState::ColorAttachment:
        return "color-attachment";
    case ResourceState::DepthWrite:
        return "depth-write";
    case ResourceState::DepthRead:
        return "depth-read";
    case ResourceState::TransferSource:
        return "transfer-source";
    case ResourceState::TransferDestination:
        return "transfer-destination";
    case ResourceState::Present:
        return "present";
    default:
        return "undefined";
    }
}

const char* PipelineStageName(PipelineStage stage)
{
    switch (stage)
    {
    case PipelineStage::Vertex:
        return "vertex";
    case PipelineStage::Fragment:
        return "fragment";
    case PipelineStage::EarlyDepth:
        return "early-depth";
    case PipelineStage::LateDepth:
        return "late-depth";
    case PipelineStage::ColorOutput:
        return "color-output";
    case PipelineStage::Compute:
        return "compute";
    case PipelineStage::AllShaders:
        return "all-shaders";
    case PipelineStage::Transfer:
        return "transfer";
    case PipelineStage::Present:
        return "present";
    default:
        return "none";
    }
}

} // namespace engine::rendergraph
