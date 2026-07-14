#include "engine/backend/gl/GLRenderBackend.h"

#include "engine/backend/gl/GLDebug.h"
#include "engine/render/SceneRenderer.h"
#include "engine/scene/Scene.h"

#include <glad/gl.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace engine
{

void GLRenderBackend::PrepareOcclusionFrame(
    const RenderFrameData& frame,
    std::vector<const PreparedRenderCommand*>& visibleCommands)
{
    const VisibilitySettings& settings = frame.SceneData->Visibility;
    const bool historyReset = m_occlusionState.BeginFrame(
        BuildOcclusionFrameSignature(frame), settings);
    if (historyReset)
        m_hizValid = false;

    m_frameStats.GpuOcclusionActive = m_occlusionState.Active();
    m_frameStats.GpuOcclusionHistoryReset = historyReset;
    m_frameStats.GpuOcclusionCandidates = m_occlusionState.Active()
        ? static_cast<uint32_t>(std::count_if(
            frame.RenderCommands.begin(), frame.RenderCommands.end(),
            [](const PreparedRenderCommand& command)
            { return !command.Source->AlwaysVisible; }))
        : 0u;
    m_frameStats.GpuOcclusionCulled = 0;
    m_frameStats.GpuOcclusionResultsConsumed = 0;
    m_frameStats.HiZMipLevels = m_occlusionState.Active()
        ? static_cast<uint32_t>(m_hizMipLevels) : 0u;
    m_frameStats.OcclusionReadbackLatencyFrames = 0;
    m_occlusionCulledInstances.clear();

    for (OcclusionReadbackSlot& slot : m_occlusionSlots)
    {
        if (!slot.Fence)
            continue;
        GLsync fence = reinterpret_cast<GLsync>(slot.Fence);
        const GLenum status = glClientWaitSync(fence, 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
            continue;
        std::vector<uint32_t> visibleBits(slot.Records.size(), 1u);
        if (!visibleBits.empty())
        {
            std::memcpy(visibleBits.data(), slot.ReadbackMapped,
                        visibleBits.size() * sizeof(uint32_t));
            if (slot.Generation == m_occlusionState.Generation())
            {
                m_occlusionState.ApplyResults(slot.Generation, slot.Records,
                                               visibleBits);
                m_frameStats.GpuOcclusionResultsConsumed +=
                    static_cast<uint32_t>(visibleBits.size());
                m_frameStats.OcclusionReadbackLatencyFrames =
                    static_cast<uint32_t>(m_occlusionState.FrameIndex() -
                                          slot.SubmittedFrame);
            }
        }
        glDeleteSync(fence);
        slot.Fence = nullptr;
        slot.Records.clear();
    }

    visibleCommands.reserve(frame.RenderCommands.size());
    for (const PreparedRenderCommand& command : frame.RenderCommands)
    {
        const OcclusionQueryRecord record = MakeOcclusionQueryRecord(command);
        if (command.Source->AlwaysVisible || m_occlusionState.ShouldDraw(record))
            visibleCommands.push_back(&command);
        else
        {
            m_occlusionCulledInstances.insert(command.InstanceIndex);
            ++m_frameStats.GpuOcclusionCulled;
        }
    }
}

void GLRenderBackend::DispatchOcclusionQueries(const RenderFrameData& frame)
{
    const auto emptyProfilePass = [this]
    {
        BeginGpuProfilerPass(OcclusionCullPass);
        EndGpuProfilerPass();
    };
    if (!m_occlusionState.Active() || !m_hizValid ||
        frame.RenderCommands.empty())
    {
        emptyProfilePass();
        return;
    }

    OcclusionReadbackSlot* slot = nullptr;
    for (uint32_t offset = 0; offset < kOcclusionReadbackSlots; ++offset)
    {
        const uint32_t index = (m_occlusionWriteSlot + offset) %
                               kOcclusionReadbackSlots;
        if (!m_occlusionSlots[index].Fence)
        {
            slot = &m_occlusionSlots[index];
            m_occlusionWriteSlot = (index + 1) % kOcclusionReadbackSlots;
            break;
        }
    }
    if (!slot)
    {
        emptyProfilePass();
        return;
    }

    const size_t count = std::count_if(
        frame.RenderCommands.begin(), frame.RenderCommands.end(),
        [](const PreparedRenderCommand& command)
        { return !command.Source->AlwaysVisible; });
    if (count == 0)
    {
        emptyProfilePass();
        return;
    }
    if (slot->CandidateBuffer == 0)
    {
        glCreateBuffers(1, &slot->CandidateBuffer);
        glCreateBuffers(1, &slot->ResultBuffer);
        glCreateBuffers(1, &slot->ReadbackBuffer);
        gl_debug::LabelObject(GL_BUFFER, slot->CandidateBuffer,
                              "Hi-Z Occlusion Candidate Bounds");
        gl_debug::LabelObject(GL_BUFFER, slot->ResultBuffer,
                              "Hi-Z Occlusion GPU Visibility Results");
        gl_debug::LabelObject(GL_BUFFER, slot->ReadbackBuffer,
                              "Hi-Z Occlusion Async Readback Staging");
    }
    if (slot->Capacity < count)
    {
        if (slot->ReadbackMapped)
        {
            glUnmapNamedBuffer(slot->ReadbackBuffer);
            slot->ReadbackMapped = nullptr;
        }
        if (slot->ReadbackBuffer)
            glDeleteBuffers(1, &slot->ReadbackBuffer);
        glCreateBuffers(1, &slot->ReadbackBuffer);
        gl_debug::LabelObject(GL_BUFFER, slot->ReadbackBuffer,
                              "Hi-Z Occlusion Async Readback Staging");

        slot->Capacity = std::max<size_t>(64, std::bit_ceil(count));
        glNamedBufferData(slot->CandidateBuffer,
            static_cast<GLsizeiptr>(slot->Capacity * sizeof(GpuOcclusionBounds)),
            nullptr, GL_DYNAMIC_DRAW);
        const GLsizeiptr resultBytes = static_cast<GLsizeiptr>(
            slot->Capacity * sizeof(uint32_t));
        glNamedBufferData(slot->ResultBuffer, resultBytes, nullptr,
                          GL_DYNAMIC_COPY);
        constexpr GLbitfield storageFlags = GL_MAP_READ_BIT |
            GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT |
            GL_CLIENT_STORAGE_BIT;
        glNamedBufferStorage(slot->ReadbackBuffer, resultBytes, nullptr,
                             storageFlags);
        slot->ReadbackMapped = static_cast<const uint32_t*>(
            glMapNamedBufferRange(slot->ReadbackBuffer, 0, resultBytes,
                GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT));
        if (!slot->ReadbackMapped)
            throw std::runtime_error(
                "Failed to map the OpenGL Hi-Z readback staging buffer");
    }

    std::vector<GpuOcclusionBounds> candidates;
    candidates.reserve(count);
    slot->Records.clear();
    slot->Records.reserve(count);
    const float inflation = frame.SceneData->Visibility.OcclusionBoundsInflation;
    for (const PreparedRenderCommand& command : frame.RenderCommands)
    {
        if (command.Source->AlwaysVisible)
            continue;
        candidates.push_back(MakeGpuOcclusionBounds(command, inflation));
        slot->Records.push_back(MakeOcclusionQueryRecord(command));
    }
    glNamedBufferSubData(slot->CandidateBuffer, 0,
        static_cast<GLsizeiptr>(candidates.size() * sizeof(GpuOcclusionBounds)),
        candidates.data());

    gl_debug::ScopedGroup marker("Visibility / Hi-Z Occlusion Cull");
    m_occlusionTestShader->Use();
    m_occlusionTestShader->SetMat4("uViewProjection", m_hizViewProjection);
    m_occlusionTestShader->SetFloat("uDepthBias",
        std::max(frame.SceneData->Visibility.OcclusionDepthBias, 0.0f));
    m_occlusionTestShader->SetInt("uMaxMip", m_hizMipLevels - 1);
    m_occlusionTestShader->SetInt("uCandidateCount", static_cast<int>(count));
    m_occlusionTestShader->SetInt("uHiZ", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hizTexture);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, slot->CandidateBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, slot->ResultBuffer);
    BeginGpuProfilerPass(OcclusionCullPass);
    glDispatchCompute(static_cast<GLuint>((count + 63u) / 64u), 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_BUFFER_UPDATE_BARRIER_BIT);
    ++m_gpuDispatchesThisFrame;
    EndGpuProfilerPass();

    // Copy only the compact visibility bitfield into persistently mapped
    // client storage. The compute SSBO stays device-local, while the fence
    // lets the CPU consume a previous frame without waiting for this copy.
    glCopyNamedBufferSubData(slot->ResultBuffer, slot->ReadbackBuffer, 0, 0,
        static_cast<GLsizeiptr>(count * sizeof(uint32_t)));
    slot->Fence = reinterpret_cast<void*>(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
    slot->Generation = m_occlusionState.Generation();
    slot->SubmittedFrame = m_occlusionState.FrameIndex();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
}

void GLRenderBackend::BuildHiZPyramid(const glm::mat4& viewProjection)
{
    if (!m_occlusionState.Active() || m_hizTexture == 0)
    {
        BeginGpuProfilerPass(HiZBuildPass);
        EndGpuProfilerPass();
        return;
    }
    gl_debug::ScopedGroup marker("Visibility / Build Hi-Z Pyramid");
    m_hizBuildShader->Use();
    m_hizBuildShader->SetInt("uSource", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    glBindImageTexture(1, m_hizTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                       GL_R32F);
    m_hizBuildShader->SetBool("uCopySource", true);
    m_hizBuildShader->SetInt("uSourceMip", 0);
    BeginGpuProfilerPass(HiZBuildPass);
    glDispatchCompute(static_cast<GLuint>((m_width + 7) / 8),
                      static_cast<GLuint>((m_height + 7) / 8), 1);
    ++m_gpuDispatchesThisFrame;
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT);

    glBindTexture(GL_TEXTURE_2D, m_hizTexture);
    m_hizBuildShader->SetBool("uCopySource", false);
    for (int mip = 1; mip < m_hizMipLevels; ++mip)
    {
        const int width = std::max(m_width >> mip, 1);
        const int height = std::max(m_height >> mip, 1);
        m_hizBuildShader->SetInt("uSourceMip", mip - 1);
        glBindImageTexture(1, m_hizTexture, mip, GL_FALSE, 0, GL_WRITE_ONLY,
                           GL_R32F);
        glDispatchCompute(static_cast<GLuint>((width + 7) / 8),
                          static_cast<GLuint>((height + 7) / 8), 1);
        ++m_gpuDispatchesThisFrame;
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
    m_hizViewProjection = viewProjection;
    m_hizValid = true;
    EndGpuProfilerPass();
}

void GLRenderBackend::DestroyOcclusionResources()
{
    for (OcclusionReadbackSlot& slot : m_occlusionSlots)
    {
        if (slot.Fence)
        {
            glDeleteSync(reinterpret_cast<GLsync>(slot.Fence));
            slot.Fence = nullptr;
        }
        if (slot.CandidateBuffer)
            glDeleteBuffers(1, &slot.CandidateBuffer);
        if (slot.ResultBuffer)
            glDeleteBuffers(1, &slot.ResultBuffer);
        if (slot.ReadbackMapped)
            glUnmapNamedBuffer(slot.ReadbackBuffer);
        if (slot.ReadbackBuffer)
            glDeleteBuffers(1, &slot.ReadbackBuffer);
        slot = {};
    }
    m_occlusionState.Reset();
    m_occlusionCulledInstances.clear();
    m_hizValid = false;
}

} // namespace engine
