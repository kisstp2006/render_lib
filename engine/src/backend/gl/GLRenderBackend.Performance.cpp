#include "engine/backend/gl/GLRenderBackend.h"

#include "engine/core/Log.h"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <string>

namespace engine {
namespace {

constexpr std::array<GLenum, 5> kPipelineTargets{
    GL_VERTICES_SUBMITTED_ARB,
    GL_PRIMITIVES_SUBMITTED_ARB,
    GL_VERTEX_SHADER_INVOCATIONS_ARB,
    GL_FRAGMENT_SHADER_INVOCATIONS_ARB,
    GL_COMPUTE_SHADER_INVOCATIONS_ARB
};

constexpr std::array<const char*, 5> kPassNames{
    "Shadows/Directional",
    "Shadows/Local lights",
    "Main HDR",
    "Post process",
    "Debug UI"
};

constexpr GLenum kGpuMemoryInfoTotalAvailableMemoryNvx = 0x9048;
constexpr GLenum kGpuMemoryInfoCurrentAvailableVidmemNvx = 0x9049;

} // namespace

void GLRenderBackend::CreateGpuProfilerQueries()
{
    if (!m_gpuTimingEnabled)
        return;
    for (auto& frame : m_gpuPassQueries)
        glGenQueries(static_cast<GLsizei>(frame.size()), frame.data());
    if (m_gpuPipelineStatisticsSupported)
        for (auto& frame : m_gpuPipelineQueries)
            glGenQueries(static_cast<GLsizei>(frame.size()), frame.data());
}

void GLRenderBackend::DestroyGpuProfilerQueries()
{
    for (auto& frame : m_gpuPassQueries)
    {
        glDeleteQueries(static_cast<GLsizei>(frame.size()), frame.data());
        frame.fill(0);
    }
    for (auto& frame : m_gpuPipelineQueries)
    {
        glDeleteQueries(static_cast<GLsizei>(frame.size()), frame.data());
        frame.fill(0);
    }
    m_gpuProfileFrameIssued.fill(false);
    m_gpuProfileFrameActive = false;
    m_shadowTiming.Reset();
    m_localShadowTiming.Reset();
    m_postTiming.Reset();
}

profiling::GpuMemoryStatistics GLRenderBackend::QueryGpuMemory() const
{
    profiling::GpuMemoryStatistics memory;
    if (!m_gpuMemoryBudgetSupported)
        return memory;

    GLint totalKilobytes = 0;
    GLint availableKilobytes = 0;
    glGetIntegerv(kGpuMemoryInfoTotalAvailableMemoryNvx, &totalKilobytes);
    glGetIntegerv(kGpuMemoryInfoCurrentAvailableVidmemNvx, &availableKilobytes);
    if (totalKilobytes > 0 && availableKilobytes >= 0)
    {
        const uint64_t total = static_cast<uint64_t>(totalKilobytes) * 1024ull;
        const uint64_t available = static_cast<uint64_t>(availableKilobytes) * 1024ull;
        memory.BudgetBytes = total;
        memory.AvailableBytes = std::min(available, total);
        memory.UsageBytes = total - memory.AvailableBytes;
    }
    return memory;
}

void GLRenderBackend::ReadGpuProfilerFrame(uint32_t slot)
{
    std::array<float, GpuProfilerPassCount> milliseconds{};
    for (uint32_t pass = 0; pass < GpuProfilerPassCount; ++pass)
    {
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(m_gpuPassQueries[slot][pass], GL_QUERY_RESULT, &nanoseconds);
        milliseconds[pass] = static_cast<float>(nanoseconds) / 1'000'000.0f;
    }

    profiling::GpuFrameProfile profile;
    profile.FrameIndex = m_gpuProfileFrameIds[slot];
    profile.BackendName = Name();
    profile.AdapterName = m_capabilities.AdapterName;
    profile.TimingAvailable = true;
    profile.Passes.reserve(GpuProfilerPassCount);
    for (uint32_t pass = 0; pass < GpuProfilerPassCount; ++pass)
    {
        profile.Passes.push_back({kPassNames[pass], milliseconds[pass]});
        profile.FrameMilliseconds += milliseconds[pass];
    }
    profile.Pipeline.DrawCalls = m_gpuProfileDrawCalls[slot];
    profile.Pipeline.ComputeDispatches = m_gpuProfileDispatches[slot];
    profile.PipelineStatisticsAvailable = m_gpuPipelineStatisticsSupported;
    if (m_gpuPipelineStatisticsSupported)
    {
        std::array<GLuint64, kGpuPipelineCounterCount> counters{};
        for (uint32_t counter = 0; counter < kGpuPipelineCounterCount; ++counter)
            glGetQueryObjectui64v(m_gpuPipelineQueries[slot][counter], GL_QUERY_RESULT,
                                  &counters[counter]);
        profile.Pipeline.InputAssemblyVertices = counters[0];
        profile.Pipeline.InputAssemblyPrimitives = counters[1];
        profile.Pipeline.VertexShaderInvocations = counters[2];
        profile.Pipeline.FragmentShaderInvocations = counters[3];
        profile.Pipeline.ComputeShaderInvocations = counters[4];
    }
    profile.MemoryBudgetAvailable = m_gpuMemoryBudgetSupported;
    profile.Memory = QueryGpuMemory();
    profiling::GpuProfiler::Get().SubmitFrame(profile);

    m_frameStats.GpuTimingAvailable = true;
    m_frameStats.GpuShadowMilliseconds = milliseconds[DirectionalShadowPass]
                                       + milliseconds[LocalShadowPass];
    m_frameStats.GpuMainMilliseconds = milliseconds[MainHdrPass];
    m_frameStats.GpuPostMilliseconds = milliseconds[PostProcessPass];
    m_frameStats.GpuFrameMilliseconds = profile.FrameMilliseconds;

    if (m_gpuProfileLogShadows[slot])
    {
        if (const auto summary = m_shadowTiming.Submit(milliseconds[DirectionalShadowPass]))
            log::Info(FormatGpuTiming("OpenGL directional shadows GPU", *summary));
        if (const auto summary = m_localShadowTiming.Submit(milliseconds[LocalShadowPass]))
            log::Info(FormatGpuTiming("OpenGL local shadows GPU", *summary));
    }
    if (m_gpuProfileLogPost[slot])
    {
        if (const auto summary = m_postTiming.Submit(milliseconds[PostProcessPass]))
        {
            const AntiAliasingMode mode = m_gpuProfileAaModes[slot];
            const char* modeName = mode == AntiAliasingMode::Taa ? "TAA"
                                 : mode == AntiAliasingMode::Fxaa ? "FXAA" : "NONE";
            log::Info(FormatGpuTiming(std::string("OpenGL post GPU (") + modeName + ")",
                                      *summary));
        }
    }
}

void GLRenderBackend::BeginGpuProfilerFrame()
{
    ++m_gpuProfileFrameIndex;
    m_gpuProfileFrameActive = false;
    if (!m_gpuTimingEnabled || !profiling::GpuProfiler::Get().IsEnabled())
        return;

    const uint32_t slot = m_gpuProfileWriteSlot;
    if (m_gpuProfileFrameIssued[slot])
    {
        GLint available = 0;
        for (uint32_t pass = 0; pass < GpuProfilerPassCount; ++pass)
        {
            glGetQueryObjectiv(m_gpuPassQueries[slot][pass], GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available)
                return; // Never stall; skip this sample when the GPU is still busy.
        }
        if (m_gpuPipelineStatisticsSupported)
        {
            for (uint32_t counter = 0; counter < kGpuPipelineCounterCount; ++counter)
            {
                glGetQueryObjectiv(m_gpuPipelineQueries[slot][counter],
                                   GL_QUERY_RESULT_AVAILABLE, &available);
                if (!available)
                    return;
            }
        }
        ReadGpuProfilerFrame(slot);
        m_gpuProfileFrameIssued[slot] = false;
    }

    m_gpuProfileActiveSlot = slot;
    m_gpuDrawCallsThisFrame = 0;
    m_gpuDispatchesThisFrame = 0;
    if (m_gpuPipelineStatisticsSupported)
        for (uint32_t counter = 0; counter < kGpuPipelineCounterCount; ++counter)
            glBeginQuery(kPipelineTargets[counter], m_gpuPipelineQueries[slot][counter]);
    m_gpuProfileFrameActive = true;
}

void GLRenderBackend::BeginGpuProfilerPass(uint32_t passIndex)
{
    if (m_gpuProfileFrameActive && passIndex < GpuProfilerPassCount)
        glBeginQuery(GL_TIME_ELAPSED, m_gpuPassQueries[m_gpuProfileActiveSlot][passIndex]);
}

void GLRenderBackend::EndGpuProfilerPass()
{
    if (m_gpuProfileFrameActive)
        glEndQuery(GL_TIME_ELAPSED);
}

void GLRenderBackend::EndGpuProfilerFrame(const Scene& scene)
{
    if (!m_gpuProfileFrameActive)
        return;
    if (m_gpuPipelineStatisticsSupported)
        for (uint32_t counter = 0; counter < kGpuPipelineCounterCount; ++counter)
            glEndQuery(kPipelineTargets[counter]);

    const uint32_t slot = m_gpuProfileActiveSlot;
    m_gpuProfileFrameIds[slot] = m_gpuProfileFrameIndex;
    m_gpuProfileDrawCalls[slot] = m_gpuDrawCallsThisFrame;
    m_gpuProfileDispatches[slot] = m_gpuDispatchesThisFrame;
    m_gpuProfileLogShadows[slot] = scene.Shadows.LogPerformance;
    m_gpuProfileLogPost[slot] = scene.PostProcess.LogPerformance;
    m_gpuProfileAaModes[slot] = scene.PostProcess.AntiAliasing;
    m_gpuProfileFrameIssued[slot] = true;
    m_gpuProfileWriteSlot = (slot + 1) % kGpuProfilerBufferedFrames;
    m_gpuProfileFrameActive = false;
}

} // namespace engine
