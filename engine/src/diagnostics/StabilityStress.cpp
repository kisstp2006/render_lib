#include "engine/diagnostics/StabilityStress.h"

#include "engine/core/Application.h"
#include "engine/core/Log.h"
#include "engine/profiling/MemoryProfiler.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace engine::diagnostics {
namespace {

std::string EscapeJson(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value)
    {
        switch (character)
        {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

void WriteResourceStats(std::ostream& output, const BackendResourceStats& stats)
{
    output << "{\"deviceLocalBytes\":" << stats.DeviceLocalBytes
           << ",\"peakDeviceLocalBytes\":" << stats.PeakDeviceLocalBytes
           << ",\"hostVisibleBytes\":" << stats.HostVisibleBytes
           << ",\"peakHostVisibleBytes\":" << stats.PeakHostVisibleBytes
           << ",\"liveNativeAllocations\":" << stats.LiveNativeAllocations
           << ",\"meshResources\":" << stats.MeshResources
           << ",\"textureResources\":" << stats.TextureResources
           << ",\"materialResources\":" << stats.MaterialResources << '}';
}

bool ResourceCountMatches(const BackendResourceStats& baseline,
                          const BackendResourceStats& current)
{
    return baseline.LiveNativeAllocations == current.LiveNativeAllocations &&
           baseline.MeshResources == current.MeshResources &&
           baseline.TextureResources == current.TextureResources &&
           baseline.MaterialResources == current.MaterialResources &&
           baseline.HostVisibleBytes == current.HostVisibleBytes;
}

} // namespace

StabilityStressRunner::StabilityStressRunner(Application& application,
                                             StabilityStressConfig config,
                                             AssetReloadCallback assetReload)
    : m_application(application), m_config(std::move(config)),
      m_assetReload(std::move(assetReload))
{
    m_config.Cycles = std::max(m_config.Cycles, 1u);
    m_config.FramesPerStage = std::max(m_config.FramesPerStage, 2u);
    m_config.BaseWidth = std::max(m_config.BaseWidth, 320);
    m_config.BaseHeight = std::max(m_config.BaseHeight, 180);
    m_config.SmallWidth = std::max(m_config.SmallWidth, 320);
    m_config.SmallHeight = std::max(m_config.SmallHeight, 180);
    m_config.TallWidth = std::max(m_config.TallWidth, 320);
    m_config.TallHeight = std::max(m_config.TallHeight, 180);
    m_report.Backend = application.GetBackend().Name();
    m_report.RequestedCycles = m_config.Cycles;
    m_report.Stages.reserve(static_cast<size_t>(m_config.Cycles) * 12u + 1u);
    m_report.Errors.reserve(m_config.Cycles);
    application.GetDebugOverlay().SetValue("STABILITY", "STATE", "WARMUP");
    application.GetDebugOverlay().SetValue("STABILITY", "CYCLE",
        "1 / " + std::to_string(m_config.Cycles));
}

const char* StabilityStressRunner::PhaseName(Phase phase)
{
    switch (phase)
    {
    case Phase::Warmup: return "warmup";
    case Phase::ResizeSmall: return "resize-small";
    case Phase::ResizeTall: return "resize-tall";
    case Phase::ResizeBase: return "resize-base";
    case Phase::Minimize: return "minimize";
    case Phase::Restore: return "restore";
    case Phase::Borderless: return "borderless-fullscreen";
    case Phase::WindowedAfterBorderless: return "restore-from-borderless";
    case Phase::Exclusive: return "exclusive-fullscreen";
    case Phase::WindowedAfterExclusive: return "restore-from-exclusive";
    case Phase::HotReload: return "asset-and-renderer-hot-reload";
    case Phase::Validate: return "resource-lifetime-validation";
    }
    return "unknown";
}

void StabilityStressRunner::EnterPhase()
{
    Window& window = m_application.GetWindow();
    m_application.GetDebugOverlay().SetValue("STABILITY", "STATE", PhaseName(m_phase));
    switch (m_phase)
    {
    case Phase::Warmup:
        m_application.SetWindowMode(WindowMode::WindowedResizable, m_config.Monitor,
                                    m_config.BaseWidth, m_config.BaseHeight);
        break;
    case Phase::ResizeSmall:
        window.SetSize(m_config.SmallWidth, m_config.SmallHeight);
        break;
    case Phase::ResizeTall:
        window.SetSize(m_config.TallWidth, m_config.TallHeight);
        break;
    case Phase::ResizeBase:
        window.SetSize(m_config.BaseWidth, m_config.BaseHeight);
        break;
    case Phase::Minimize:
        window.Minimize();
        break;
    case Phase::Restore:
        window.Restore();
        m_application.SetWindowMode(WindowMode::WindowedResizable, m_config.Monitor,
                                    m_config.BaseWidth, m_config.BaseHeight);
        break;
    case Phase::Borderless:
        m_application.SetWindowMode(WindowMode::BorderlessFullscreen, m_config.Monitor);
        break;
    case Phase::WindowedAfterBorderless:
        m_application.SetWindowMode(WindowMode::WindowedResizable, m_config.Monitor,
                                    m_config.BaseWidth, m_config.BaseHeight);
        break;
    case Phase::Exclusive:
        if (m_config.ExerciseExclusiveFullscreen)
            m_application.SetWindowMode(WindowMode::ExclusiveFullscreen, m_config.Monitor);
        break;
    case Phase::WindowedAfterExclusive:
        m_application.SetWindowMode(WindowMode::WindowedResizable, m_config.Monitor,
                                    m_config.BaseWidth, m_config.BaseHeight);
        break;
    case Phase::HotReload:
        if (m_assetReload)
            m_assetReload();
        m_application.ReloadRenderer();
        ++m_report.HotReloadCount;
        break;
    case Phase::Validate:
        break;
    }
}

void StabilityStressRunner::RecordStage(const char* name)
{
    StabilityStressStageResult result;
    result.Name = name;
    result.Cycle = m_cycle + 1u;
    result.Width = m_application.GetWindow().Width();
    result.Height = m_application.GetWindow().Height();
    result.Minimized = m_application.GetWindow().IsMinimized();
    result.CpuBytes = profiling::MemoryProfiler::Get().Snapshot().CurrentBytes;
    result.Resources = m_application.GetBackend().GetResourceStats();
    m_report.Stages.push_back(std::move(result));
}

void StabilityStressRunner::ValidateCycle()
{
    StabilityStressStageResult& stage = m_report.Stages.back();
    const BackendResourceStats& current = stage.Resources;
    const uint64_t maximumDeviceBytes = m_report.BaselineResources.DeviceLocalBytes +
                                        m_config.MaximumGpuGrowthBytes;
    const bool resourceCountsStable = ResourceCountMatches(m_report.BaselineResources, current);
    const bool deviceMemoryStable = current.DeviceLocalBytes <= maximumDeviceBytes;
    const bool cpuMemoryStable = stage.CpuBytes <= m_report.BaselineCpuBytes +
                                                   m_config.MaximumCpuGrowthBytes;
    if (!resourceCountsStable || !deviceMemoryStable || !cpuMemoryStable)
    {
        std::ostringstream message;
        message << "cycle " << (m_cycle + 1u) << " lifetime drift:";
        if (!resourceCountsStable)
            message << " native/cache allocation counts changed";
        if (!deviceMemoryStable)
            message << " GPU bytes grew by "
                    << (current.DeviceLocalBytes - m_report.BaselineResources.DeviceLocalBytes);
        if (!cpuMemoryStable)
            message << " CPU bytes grew by " << (stage.CpuBytes - m_report.BaselineCpuBytes);
        stage.Passed = false;
        stage.Message = message.str();
        m_report.Passed = false;
        m_report.Errors.push_back(stage.Message);
        log::Error("Stability stress: " + stage.Message);
    }
    else
    {
        stage.Message = "resource counts and memory returned to the baseline envelope";
        log::Info("Stability stress: cycle " + std::to_string(m_cycle + 1u) + " passed");
    }
    m_report.FinalResources = current;
    m_report.FinalCpuBytes = stage.CpuBytes;
}

void StabilityStressRunner::CompletePhase()
{
    RecordStage(PhaseName(m_phase));
    if (m_phase == Phase::Warmup)
    {
        m_report.BaselineResources = m_report.Stages.back().Resources;
        m_report.BaselineCpuBytes = m_report.Stages.back().CpuBytes;
        m_phase = Phase::ResizeSmall;
    }
    else if (m_phase == Phase::Validate)
    {
        ValidateCycle();
        ++m_cycle;
        m_report.CompletedCycles = m_cycle;
        if (m_cycle >= m_config.Cycles)
        {
            Finish();
            return;
        }
        m_application.GetDebugOverlay().SetValue("STABILITY", "CYCLE",
            std::to_string(m_cycle + 1u) + " / " + std::to_string(m_config.Cycles));
        m_phase = Phase::ResizeSmall;
    }
    else
    {
        m_phase = static_cast<Phase>(static_cast<uint8_t>(m_phase) + 1u);
        if (m_phase == Phase::Exclusive && !m_config.ExerciseExclusiveFullscreen)
            m_phase = Phase::WindowedAfterExclusive;
    }
    m_phaseFrame = 0;
    m_phaseEntered = false;
}

void StabilityStressRunner::Fail(std::string message)
{
    m_report.Passed = false;
    m_report.Errors.push_back(std::move(message));
    if (!m_report.Stages.empty())
    {
        m_report.Stages.back().Passed = false;
        m_report.Stages.back().Message = m_report.Errors.back();
    }
    log::Error("Stability stress failed: " + m_report.Errors.back());
    Finish();
}

void StabilityStressRunner::Finish()
{
    if (m_report.Completed)
        return;
    m_report.Completed = true;
    try
    {
        m_application.GetWindow().Restore();
        m_application.SetWindowMode(WindowMode::WindowedResizable, m_config.Monitor,
                                    m_config.BaseWidth, m_config.BaseHeight);
    }
    catch (...)
    {
        m_report.Passed = false;
        m_report.Errors.emplace_back("failed to restore the window after the stress run");
    }
    m_application.GetDebugOverlay().SetValue("STABILITY", "STATE",
        m_report.Passed ? "PASSED" : "FAILED");
    m_application.RequestQuit();
}

void StabilityStressRunner::Update(float)
{
    if (m_report.Completed)
        return;
    try
    {
        if (!m_phaseEntered)
        {
            EnterPhase();
            m_phaseEntered = true;
        }
        ++m_phaseFrame;
        if (m_phaseFrame >= m_config.FramesPerStage)
            CompletePhase();
    }
    catch (const std::exception& error)
    {
        Fail(std::string(PhaseName(m_phase)) + ": " + error.what());
    }
}

bool StabilityStressRunner::WriteJsonReport(const std::filesystem::path& path) const
{
    std::error_code error;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    std::ofstream output(path, std::ios::trunc);
    if (!output)
        return false;

    output << "{\n  \"backend\":\"" << EscapeJson(m_report.Backend) << "\",\n"
           << "  \"requestedCycles\":" << m_report.RequestedCycles << ",\n"
           << "  \"completedCycles\":" << m_report.CompletedCycles << ",\n"
           << "  \"hotReloadCount\":" << m_report.HotReloadCount << ",\n"
           << "  \"completed\":" << (m_report.Completed ? "true" : "false") << ",\n"
           << "  \"passed\":" << (m_report.Passed ? "true" : "false") << ",\n"
           << "  \"baselineCpuBytes\":" << m_report.BaselineCpuBytes << ",\n"
           << "  \"finalCpuBytes\":" << m_report.FinalCpuBytes << ",\n"
           << "  \"baselineResources\":";
    WriteResourceStats(output, m_report.BaselineResources);
    output << ",\n  \"finalResources\":";
    WriteResourceStats(output, m_report.FinalResources);
    output << ",\n  \"stages\":[\n";
    for (size_t index = 0; index < m_report.Stages.size(); ++index)
    {
        const StabilityStressStageResult& stage = m_report.Stages[index];
        output << "    {\"name\":\"" << EscapeJson(stage.Name) << "\",\"cycle\":"
               << stage.Cycle << ",\"width\":" << stage.Width << ",\"height\":"
               << stage.Height << ",\"minimized\":" << (stage.Minimized ? "true" : "false")
               << ",\"cpuBytes\":" << stage.CpuBytes << ",\"passed\":"
               << (stage.Passed ? "true" : "false") << ",\"message\":\""
               << EscapeJson(stage.Message) << "\",\"resources\":";
        WriteResourceStats(output, stage.Resources);
        output << '}' << (index + 1u == m_report.Stages.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"errors\":[";
    for (size_t index = 0; index < m_report.Errors.size(); ++index)
    {
        output << (index == 0 ? "" : ",") << '"' << EscapeJson(m_report.Errors[index]) << '"';
    }
    output << "]\n}\n";
    return static_cast<bool>(output);
}

} // namespace engine::diagnostics
