#include "engine/profiling/GpuProfiler.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <utility>

namespace engine::profiling {
namespace {

std::string JsonString(std::string_view text)
{
    std::ostringstream output;
    output << '"';
    for (const char character : text)
    {
        switch (character)
        {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default: output << character; break;
        }
    }
    output << '"';
    return output.str();
}

} // namespace

struct GpuProfiler::Impl
{
    mutable std::mutex Mutex;
    GpuProfilerConfig Config;
    std::vector<GpuFrameProfile> Frames;
    uint64_t PeakUsageBytes = 0;
};

GpuProfiler& GpuProfiler::Get()
{
    static GpuProfiler profiler;
    return profiler;
}

GpuProfiler::GpuProfiler() : m_impl(new Impl) {}
GpuProfiler::~GpuProfiler() { delete m_impl; }

void GpuProfiler::Configure(const GpuProfilerConfig& config)
{
    std::scoped_lock lock(m_impl->Mutex);
    m_impl->Config = config;
    m_impl->Config.RetainedFrames = std::max(config.RetainedFrames, 1u);
    if (m_impl->Frames.size() > m_impl->Config.RetainedFrames)
    {
        m_impl->Frames.erase(m_impl->Frames.begin(),
            m_impl->Frames.end() - m_impl->Config.RetainedFrames);
    }
}

void GpuProfiler::SetEnabled(bool enabled)
{
    std::scoped_lock lock(m_impl->Mutex);
    m_impl->Config.Enabled = enabled;
}

bool GpuProfiler::IsEnabled() const
{
    std::scoped_lock lock(m_impl->Mutex);
    return m_impl->Config.Enabled;
}

void GpuProfiler::Reset()
{
    std::scoped_lock lock(m_impl->Mutex);
    m_impl->Frames.clear();
    m_impl->PeakUsageBytes = 0;
}

void GpuProfiler::SubmitFrame(GpuFrameProfile frame)
{
    std::scoped_lock lock(m_impl->Mutex);
    if (!m_impl->Config.Enabled)
        return;

    if (frame.FrameMilliseconds <= 0.0f && frame.TimingAvailable)
    {
        for (const GpuPassTiming& pass : frame.Passes)
            frame.FrameMilliseconds += std::max(pass.Milliseconds, 0.0f);
    }
    m_impl->PeakUsageBytes = std::max(m_impl->PeakUsageBytes, frame.Memory.UsageBytes);
    frame.Memory.PeakUsageBytes = m_impl->PeakUsageBytes;
    m_impl->Frames.push_back(std::move(frame));
    if (m_impl->Frames.size() > m_impl->Config.RetainedFrames)
        m_impl->Frames.erase(m_impl->Frames.begin());
}

GpuProfileSnapshot GpuProfiler::Snapshot() const
{
    std::scoped_lock lock(m_impl->Mutex);
    GpuProfileSnapshot snapshot;
    snapshot.Enabled = m_impl->Config.Enabled;
    snapshot.Frames = m_impl->Frames;
    if (m_impl->Frames.empty())
        return snapshot;

    const GpuFrameProfile& latest = m_impl->Frames.back();
    snapshot.CompletedFrame = latest.FrameIndex;
    snapshot.BackendName = latest.BackendName;
    snapshot.AdapterName = latest.AdapterName;
    snapshot.TimingAvailable = latest.TimingAvailable;
    snapshot.PipelineStatisticsAvailable = latest.PipelineStatisticsAvailable;
    snapshot.MemoryBudgetAvailable = latest.MemoryBudgetAvailable;
    snapshot.FrameMilliseconds = latest.FrameMilliseconds;
    snapshot.Pipeline = latest.Pipeline;
    snapshot.Memory = latest.Memory;
    snapshot.Memory.PeakUsageBytes = m_impl->PeakUsageBytes;

    struct Accumulator
    {
        double Total = 0.0;
        float Minimum = std::numeric_limits<float>::max();
        float Maximum = 0.0f;
        float Latest = 0.0f;
        uint32_t Samples = 0;
    };
    std::map<std::string, Accumulator> accumulators;
    std::vector<std::string> order;
    for (const GpuFrameProfile& frame : m_impl->Frames)
    {
        if (!frame.TimingAvailable)
            continue;
        for (const GpuPassTiming& pass : frame.Passes)
        {
            auto [found, inserted] = accumulators.try_emplace(pass.Name);
            if (inserted)
                order.push_back(pass.Name);
            Accumulator& accumulator = found->second;
            accumulator.Total += pass.Milliseconds;
            accumulator.Minimum = std::min(accumulator.Minimum, pass.Milliseconds);
            accumulator.Maximum = std::max(accumulator.Maximum, pass.Milliseconds);
            accumulator.Latest = pass.Milliseconds;
            ++accumulator.Samples;
        }
    }
    snapshot.Passes.reserve(order.size());
    for (const std::string& name : order)
    {
        const Accumulator& accumulator = accumulators.at(name);
        GpuPassSummary summary;
        summary.Name = name;
        summary.LatestMilliseconds = accumulator.Latest;
        summary.AverageMilliseconds = accumulator.Samples > 0
            ? static_cast<float>(accumulator.Total / accumulator.Samples) : 0.0f;
        summary.MinimumMilliseconds = accumulator.Samples > 0 ? accumulator.Minimum : 0.0f;
        summary.MaximumMilliseconds = accumulator.Maximum;
        summary.SampleCount = accumulator.Samples;
        snapshot.Passes.push_back(std::move(summary));
    }
    return snapshot;
}

bool GpuProfiler::WriteJsonReport(const std::string& path) const
{
    const GpuProfileSnapshot snapshot = Snapshot();
    std::ofstream output(path, std::ios::trunc);
    if (!output)
        return false;

    output << std::fixed << std::setprecision(4);
    output << "{\n"
           << "  \"enabled\": " << (snapshot.Enabled ? "true" : "false") << ",\n"
           << "  \"backend\": " << JsonString(snapshot.BackendName) << ",\n"
           << "  \"adapter\": " << JsonString(snapshot.AdapterName) << ",\n"
           << "  \"timingAvailable\": " << (snapshot.TimingAvailable ? "true" : "false") << ",\n"
           << "  \"pipelineStatisticsAvailable\": "
           << (snapshot.PipelineStatisticsAvailable ? "true" : "false") << ",\n"
           << "  \"memoryBudgetAvailable\": "
           << (snapshot.MemoryBudgetAvailable ? "true" : "false") << ",\n"
           << "  \"passes\": [\n";
    for (size_t index = 0; index < snapshot.Passes.size(); ++index)
    {
        const GpuPassSummary& pass = snapshot.Passes[index];
        output << "    {\"name\": " << JsonString(pass.Name)
               << ", \"latestMs\": " << pass.LatestMilliseconds
               << ", \"averageMs\": " << pass.AverageMilliseconds
               << ", \"minimumMs\": " << pass.MinimumMilliseconds
               << ", \"maximumMs\": " << pass.MaximumMilliseconds
               << ", \"samples\": " << pass.SampleCount << "}"
               << (index + 1 < snapshot.Passes.size() ? "," : "") << "\n";
    }
    output << "  ],\n  \"frames\": [\n";
    for (size_t frameIndex = 0; frameIndex < snapshot.Frames.size(); ++frameIndex)
    {
        const GpuFrameProfile& frame = snapshot.Frames[frameIndex];
        output << "    {\"frame\": " << frame.FrameIndex
               << ", \"frameMs\": " << frame.FrameMilliseconds
               << ", \"drawCalls\": " << frame.Pipeline.DrawCalls
               << ", \"computeDispatches\": " << frame.Pipeline.ComputeDispatches
               << ", \"iaVertices\": " << frame.Pipeline.InputAssemblyVertices
               << ", \"iaPrimitives\": " << frame.Pipeline.InputAssemblyPrimitives
               << ", \"vertexInvocations\": " << frame.Pipeline.VertexShaderInvocations
               << ", \"fragmentInvocations\": " << frame.Pipeline.FragmentShaderInvocations
               << ", \"computeInvocations\": " << frame.Pipeline.ComputeShaderInvocations
               << ", \"vramUsageBytes\": " << frame.Memory.UsageBytes
               << ", \"vramBudgetBytes\": " << frame.Memory.BudgetBytes
               << ", \"engineOwnedBytes\": " << frame.Memory.EngineOwnedBytes
               << ", \"passTimings\": [";
        for (size_t passIndex = 0; passIndex < frame.Passes.size(); ++passIndex)
        {
            const GpuPassTiming& pass = frame.Passes[passIndex];
            output << "{\"name\": " << JsonString(pass.Name)
                   << ", \"ms\": " << pass.Milliseconds << "}"
                   << (passIndex + 1 < frame.Passes.size() ? ", " : "");
        }
        output << "]}" << (frameIndex + 1 < snapshot.Frames.size() ? "," : "") << "\n";
    }
    output << "  ],\n"
           << "  \"memory\": {\"usageBytes\": " << snapshot.Memory.UsageBytes
           << ", \"budgetBytes\": " << snapshot.Memory.BudgetBytes
           << ", \"availableBytes\": " << snapshot.Memory.AvailableBytes
           << ", \"engineOwnedBytes\": " << snapshot.Memory.EngineOwnedBytes
           << ", \"peakUsageBytes\": " << snapshot.Memory.PeakUsageBytes << "}\n"
           << "}\n";
    return output.good();
}

} // namespace engine::profiling
