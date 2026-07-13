#include "engine/profiling/CpuProfiler.h"
#include "engine/profiling/MemoryProfiler.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

namespace engine::profiling {
namespace {

thread_local std::vector<uint64_t> g_zoneStack;

std::string EscapeJson(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 8);
    for (const char character : value)
    {
        switch (character)
        {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20)
                result += '?';
            else
                result += character;
            break;
        }
    }
    return result;
}

std::vector<CpuProfileZoneSummary> BuildSummaries(
    const std::vector<CpuProfileEvent>& events,
    uint64_t firstFrame = 0,
    uint64_t lastFrame = std::numeric_limits<uint64_t>::max())
{
    struct Accumulator
    {
        std::string Name;
        std::string Category;
        double Total = 0.0;
        double Minimum = std::numeric_limits<double>::max();
        double Maximum = 0.0;
        uint64_t Count = 0;
    };

    std::unordered_map<std::string, Accumulator> accumulators;
    for (const CpuProfileEvent& event : events)
    {
        if (event.FrameIndex < firstFrame || event.FrameIndex > lastFrame)
            continue;
        const std::string key = event.Category + '\x1f' + event.Name;
        Accumulator& value = accumulators[key];
        value.Name = event.Name;
        value.Category = event.Category;
        value.Total += event.DurationMicroseconds / 1000.0;
        value.Minimum = std::min(value.Minimum, event.DurationMicroseconds / 1000.0);
        value.Maximum = std::max(value.Maximum, event.DurationMicroseconds / 1000.0);
        ++value.Count;
    }

    std::vector<CpuProfileZoneSummary> summaries;
    summaries.reserve(accumulators.size());
    for (const auto& [key, value] : accumulators)
    {
        (void)key;
        if (value.Count == 0)
            continue;
        summaries.push_back({
            value.Name, value.Category, value.Total / static_cast<double>(value.Count),
            value.Minimum, value.Maximum, value.Total, value.Count});
    }
    std::sort(summaries.begin(), summaries.end(), [](const auto& left, const auto& right) {
        return left.TotalMilliseconds > right.TotalMilliseconds;
    });
    return summaries;
}

} // namespace

CpuProfiler& CpuProfiler::Get()
{
    static CpuProfiler profiler;
    return profiler;
}

CpuProfiler::CpuProfiler()
    : m_origin(std::chrono::steady_clock::now())
{
}

void CpuProfiler::Configure(const CpuProfilerConfig& config)
{
    ENGINE_MEMORY_TAG_SCOPE("Profiling");
    std::scoped_lock lock(m_mutex);
    m_config = config;
    m_config.RetainedFrames = std::max(m_config.RetainedFrames, 1u);
    m_config.MaximumEvents = std::max<size_t>(m_config.MaximumEvents, 1);
    m_enabled.store(m_config.Enabled, std::memory_order_release);
}

void CpuProfiler::SetEnabled(bool enabled)
{
    m_enabled.store(enabled, std::memory_order_release);
}

void CpuProfiler::Reset()
{
    ENGINE_MEMORY_TAG_SCOPE("Profiling");
    std::scoped_lock lock(m_mutex);
    m_events.clear();
    m_threadNames.clear();
    m_currentFrame.store(0, std::memory_order_relaxed);
    m_nextEventId.store(1, std::memory_order_relaxed);
    m_completedFrames = 0;
    m_droppedEvents = 0;
    m_origin = std::chrono::steady_clock::now();
    g_zoneStack.clear();
}

void CpuProfiler::BeginFrame()
{
    if (!IsEnabled())
        return;
    m_currentFrame.store(m_completedFrames, std::memory_order_release);
}

void CpuProfiler::EndFrame()
{
    if (!IsEnabled())
        return;

    const uint64_t completedFrame = m_currentFrame.load(std::memory_order_acquire);
    ++m_completedFrames;
    {
        std::scoped_lock lock(m_mutex);
        const uint64_t firstRetainedFrame = m_completedFrames > m_config.RetainedFrames
            ? m_completedFrames - m_config.RetainedFrames : 0;
        std::erase_if(m_events, [&](const CpuProfileEvent& event) {
            return event.FrameIndex < firstRetainedFrame;
        });
    }

    if (m_config.LogIntervalFrames > 0
        && m_completedFrames % m_config.LogIntervalFrames == 0)
    {
        LogFrameStatistics(completedFrame);
    }
}

uint64_t CpuProfiler::ThreadId()
{
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

void CpuProfiler::SetThreadName(std::string_view name)
{
    ENGINE_MEMORY_TAG_SCOPE("Profiling");
    if (!IsEnabled())
        return;
    std::scoped_lock lock(m_mutex);
    m_threadNames[ThreadId()] = std::string(name);
}

double CpuProfiler::NowMicroseconds() const
{
    return std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - m_origin).count();
}

CpuProfiler::ZoneToken CpuProfiler::BeginZone()
{
    ZoneToken token;
    if (!IsEnabled())
        return token;

    token.Id = m_nextEventId.fetch_add(1, std::memory_order_relaxed);
    token.ParentId = g_zoneStack.empty() ? 0 : g_zoneStack.back();
    token.FrameIndex = m_currentFrame.load(std::memory_order_acquire);
    token.ThreadId = ThreadId();
    token.Depth = static_cast<uint32_t>(g_zoneStack.size());
    token.StartMicroseconds = NowMicroseconds();
    token.Active = true;
    g_zoneStack.push_back(token.Id);
    return token;
}

void CpuProfiler::EndZone(ZoneToken token, std::string name, std::string category)
{
    ENGINE_MEMORY_TAG_SCOPE("Profiling");
    if (!token.Active)
        return;
    const double duration = std::max(NowMicroseconds() - token.StartMicroseconds, 0.0);
    if (!g_zoneStack.empty())
    {
        if (g_zoneStack.back() == token.Id)
            g_zoneStack.pop_back();
        else if (const auto found = std::find(g_zoneStack.begin(), g_zoneStack.end(), token.Id);
                 found != g_zoneStack.end())
            g_zoneStack.erase(found);
    }

    std::scoped_lock lock(m_mutex);
    if (m_events.size() >= m_config.MaximumEvents)
    {
        const size_t removeCount = std::max<size_t>(m_config.MaximumEvents / 20, 1);
        const size_t actualCount = std::min(removeCount, m_events.size());
        m_events.erase(m_events.begin(), m_events.begin() + static_cast<std::ptrdiff_t>(actualCount));
        m_droppedEvents += actualCount;
    }
    m_events.push_back({token.Id, token.ParentId, token.FrameIndex, token.ThreadId,
                        token.Depth, token.StartMicroseconds, duration,
                        std::move(name), std::move(category)});
}

CpuProfileSnapshot CpuProfiler::Snapshot() const
{
    ENGINE_MEMORY_TAG_SCOPE("Profiling");
    CpuProfileSnapshot snapshot;
    {
        std::scoped_lock lock(m_mutex);
        snapshot.Events = m_events;
        snapshot.ThreadNames = m_threadNames;
        snapshot.DroppedEvents = m_droppedEvents;
    }
    std::sort(snapshot.Events.begin(), snapshot.Events.end(), [](const auto& left, const auto& right) {
        if (left.StartMicroseconds != right.StartMicroseconds)
            return left.StartMicroseconds < right.StartMicroseconds;
        return left.Id < right.Id;
    });
    if (!snapshot.Events.empty())
    {
        snapshot.FirstFrame = snapshot.Events.front().FrameIndex;
        snapshot.LastFrame = snapshot.Events.front().FrameIndex;
        for (const CpuProfileEvent& event : snapshot.Events)
        {
            snapshot.FirstFrame = std::min(snapshot.FirstFrame, event.FrameIndex);
            snapshot.LastFrame = std::max(snapshot.LastFrame, event.FrameIndex);
        }
    }
    snapshot.Summaries = BuildSummaries(snapshot.Events);
    return snapshot;
}

void CpuProfiler::LogFrameStatistics(uint64_t completedFrame)
{
    std::vector<CpuProfileEvent> events;
    uint32_t interval = 0;
    {
        std::scoped_lock lock(m_mutex);
        events = m_events;
        interval = m_config.LogIntervalFrames;
    }
    const uint64_t firstFrame = completedFrame + 1 > interval
        ? completedFrame + 1 - interval : 0;
    const auto summaries = BuildSummaries(events, firstFrame, completedFrame);
    log::Info("CPU profiler: frames " + std::to_string(firstFrame) + "-"
              + std::to_string(completedFrame));
    const size_t count = std::min<size_t>(summaries.size(), 8);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& summary = summaries[i];
        log::Info("  " + summary.Category + "/" + summary.Name
                  + ": avg " + std::to_string(summary.AverageMilliseconds)
                  + " ms, min " + std::to_string(summary.MinimumMilliseconds)
                  + " ms, max " + std::to_string(summary.MaximumMilliseconds)
                  + " ms, calls " + std::to_string(summary.CallCount));
    }
}

bool CpuProfiler::WriteChromeTrace(const std::string& path) const
{
    ENGINE_MEMORY_TAG_SCOPE("Profiling");
    const CpuProfileSnapshot snapshot = Snapshot();
    const std::filesystem::path outputPath(path);
    std::error_code error;
    if (!outputPath.parent_path().empty())
        std::filesystem::create_directories(outputPath.parent_path(), error);
    if (error)
        return false;

    std::ofstream output(outputPath, std::ios::trunc);
    if (!output)
        return false;
    output << std::fixed << std::setprecision(3);
    output << "{\n  \"displayTimeUnit\": \"ms\",\n  \"traceEvents\": [\n";
    bool first = true;
    const auto emitSeparator = [&] {
        if (!first)
            output << ",\n";
        first = false;
    };
    for (const auto& [threadId, name] : snapshot.ThreadNames)
    {
        emitSeparator();
        output << "    {\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":"
               << threadId << ",\"args\":{\"name\":\"" << EscapeJson(name) << "\"}}";
    }
    for (const CpuProfileEvent& event : snapshot.Events)
    {
        emitSeparator();
        output << "    {\"name\":\"" << EscapeJson(event.Name)
               << "\",\"cat\":\"" << EscapeJson(event.Category)
               << "\",\"ph\":\"X\",\"ts\":" << event.StartMicroseconds
               << ",\"dur\":" << event.DurationMicroseconds
               << ",\"pid\":1,\"tid\":" << event.ThreadId
               << ",\"args\":{\"frame\":" << event.FrameIndex
               << ",\"depth\":" << event.Depth
               << ",\"id\":" << event.Id
               << ",\"parent\":" << event.ParentId << "}}";
    }
    output << "\n  ],\n  \"metadata\": {\"firstFrame\":" << snapshot.FirstFrame
           << ",\"lastFrame\":" << snapshot.LastFrame
           << ",\"droppedEvents\":" << snapshot.DroppedEvents << "}\n}\n";
    return static_cast<bool>(output);
}

CpuProfileScope::CpuProfileScope(std::string_view name, std::string_view category)
{
    CpuProfiler& profiler = CpuProfiler::Get();
    if (!profiler.IsEnabled())
        return;
    m_profiler = &profiler;
    m_name = name;
    m_category = category;
    m_token = profiler.BeginZone();
}

CpuProfileScope::~CpuProfileScope()
{
    End();
}

void CpuProfileScope::End()
{
    if (!m_profiler || !m_token.Active)
        return;
    m_profiler->EndZone(m_token, std::move(m_name), std::move(m_category));
    m_token.Active = false;
}

} // namespace engine::profiling
