#pragma once

#include "engine/render/RenderGraph.h"

#include <array>
#include <cstddef>
#include <functional>

namespace engine::rendergraph
{

enum class RendererPass : uint8_t
{
    Environment,
    DirectionalShadows,
    LocalShadows,
    OcclusionCull,
    MainHdr,
    HiZBuild,
    PostProcess,
    DebugUi,
    Count
};

struct RendererFrameGraphFeatures
{
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t MsaaSamples = 1;
    uint32_t BloomLevels = 0;
    bool SunShadows = true;
    bool LocalShadows = true;
    bool TemporalAA = false;
    uint32_t TaaReadIndex = 0;
    uint32_t TaaWriteIndex = 1;
    bool Bloom = false;
    bool Fxaa = false;
    bool DebugUi = true;
    bool OcclusionCulling = false;
    uint32_t OcclusionCandidateCount = 0;
    uint32_t HiZMipLevels = 1;
};

struct RendererFrameGraphCallbacks
{
    std::array<std::function<void()>, static_cast<size_t>(RendererPass::Count)>
        Passes;

    std::function<void()>& operator[](RendererPass pass)
    {
        return Passes[static_cast<size_t>(pass)];
    }
};

bool BuildRendererFrameGraph(RenderGraph& graph, const Config& config,
                             const RendererFrameGraphFeatures& features,
                             RendererFrameGraphCallbacks callbacks,
                             std::string* error = nullptr);

} // namespace engine::rendergraph
