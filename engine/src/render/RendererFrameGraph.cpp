#include "engine/render/RendererFrameGraph.h"

#include <algorithm>
#include <utility>

namespace engine::rendergraph
{
namespace
{

ResourceDesc Texture(std::string name, std::string format, uint32_t width,
                     uint32_t height, uint32_t bytesPerPixel,
                     uint32_t samples = 1, uint32_t layers = 1)
{
    ResourceDesc desc;
    desc.Name = std::move(name);
    desc.Format = std::move(format);
    desc.Width = std::max(width, 1u);
    desc.Height = std::max(height, 1u);
    desc.BytesPerPixel = bytesPerPixel;
    desc.Samples = std::max(samples, 1u);
    desc.DepthOrLayers = std::max(layers, 1u);
    return desc;
}

std::function<void()> Callback(RendererFrameGraphCallbacks& callbacks,
                               RendererPass pass)
{
    std::function<void()>& callback = callbacks[pass];
    return callback ? std::move(callback) : [] {};
}

} // namespace

bool BuildRendererFrameGraph(RenderGraph& graph, const Config& config,
                             const RendererFrameGraphFeatures& features,
                             RendererFrameGraphCallbacks callbacks,
                             std::string* error)
{
    graph.Reset(config);
    const uint32_t width = std::max(features.Width, 1u);
    const uint32_t height = std::max(features.Height, 1u);

    auto environment = Texture("ibl.environment", "RGBA16F", 256, 256, 8, 1, 6);
    environment.InitialState = ResourceState::ShaderRead;
    environment.InitialStage = PipelineStage::Fragment;
    const ResourceHandle environmentMap = graph.Import(environment);
    auto irradiance = Texture("ibl.irradiance", "RGBA16F", 32, 32, 8, 1, 6);
    irradiance.InitialState = ResourceState::ShaderRead;
    irradiance.InitialStage = PipelineStage::Fragment;
    const ResourceHandle irradianceMap = graph.Import(irradiance);
    auto prefilter = Texture("ibl.prefilter", "RGBA16F", 128, 128, 8, 1, 6);
    prefilter.MipLevels = 8;
    prefilter.InitialState = ResourceState::ShaderRead;
    prefilter.InitialStage = PipelineStage::Fragment;
    const ResourceHandle prefilterMap = graph.Import(prefilter);
    auto brdf = Texture("ibl.brdf", "RG16F", 512, 512, 4);
    brdf.InitialState = ResourceState::ShaderRead;
    brdf.InitialStage = PipelineStage::Fragment;
    const ResourceHandle brdfLut = graph.Import(brdf);

    std::array<ResourceHandle, 4> directional{};
    for (uint32_t cascade = 0; cascade < directional.size(); ++cascade)
    {
        auto shadow = Texture("shadow.directional." + std::to_string(cascade),
                              "D32F", std::max(4096u >> cascade, 1024u),
                              std::max(4096u >> cascade, 1024u), 4);
        shadow.InitialState = ResourceState::DepthRead;
        shadow.InitialStage = PipelineStage::Fragment;
        directional[cascade] = graph.Import(std::move(shadow));
    }
    auto localShadowDesc = Texture("shadow.local", "D32F", 4096, 4096, 4);
    localShadowDesc.InitialState = ResourceState::DepthRead;
    localShadowDesc.InitialStage = PipelineStage::Fragment;
    const ResourceHandle localShadow = graph.Import(std::move(localShadowDesc));
    auto pointShadowDesc = Texture("shadow.point", "D32F", 1024, 1024, 4, 1, 24);
    pointShadowDesc.InitialState = ResourceState::DepthRead;
    pointShadowDesc.InitialStage = PipelineStage::Fragment;
    const ResourceHandle pointShadow = graph.Import(std::move(pointShadowDesc));
    auto cookieDesc = Texture("light.cookies", "R8", 2048, 2048, 1);
    cookieDesc.InitialState = ResourceState::ShaderRead;
    cookieDesc.InitialStage = PipelineStage::Fragment;
    const ResourceHandle cookies = graph.Import(std::move(cookieDesc));

    ResourceHandle msaaHdr;
    ResourceHandle msaaVelocity;
    ResourceHandle msaaDepth;
    if (features.MsaaSamples > 1)
    {
        msaaHdr = graph.Create(Texture("scene.msaa.hdr", "RGBA16F", width, height,
                                       8, features.MsaaSamples));
        msaaVelocity = graph.Create(Texture("scene.msaa.velocity", "RG16F", width,
                                            height, 4, features.MsaaSamples));
        msaaDepth = graph.Create(Texture("scene.msaa.depth", "D32F", width, height,
                                         4, features.MsaaSamples));
    }
    const ResourceHandle hdr =
        graph.Create(Texture("scene.hdr", "RGBA16F", width, height, 8));
    const ResourceHandle velocity =
        graph.Create(Texture("scene.velocity", "RG16F", width, height, 4));
    const ResourceHandle depth =
        graph.Create(Texture("scene.depth", "D32F", width, height, 4));

    std::array<ResourceHandle, 2> taaColor{};
    std::array<ResourceHandle, 2> taaDepth{};
    if (features.TemporalAA)
    {
        for (uint32_t index = 0; index < 2; ++index)
        {
            auto color = Texture("taa.color." + std::to_string(index), "RGBA16F",
                                 width, height, 8);
            color.InitialState = ResourceState::ShaderRead;
            color.InitialStage = PipelineStage::Fragment;
            taaColor[index] = graph.Import(std::move(color));
            auto historyDepth = Texture("taa.depth." + std::to_string(index), "R32F",
                                        width, height, 4);
            historyDepth.InitialState = ResourceState::ShaderRead;
            historyDepth.InitialStage = PipelineStage::Fragment;
            taaDepth[index] = graph.Import(std::move(historyDepth));
        }
    }

    std::vector<ResourceHandle> bloom;
    if (features.Bloom)
    {
        uint32_t bloomWidth = std::max(width / 2, 1u);
        uint32_t bloomHeight = std::max(height / 2, 1u);
        for (uint32_t level = 0; level < features.BloomLevels; ++level)
        {
            bloom.push_back(
                graph.Create(Texture("bloom." + std::to_string(level), "RGBA16F",
                                     bloomWidth, bloomHeight, 8)));
            bloomWidth = std::max(bloomWidth / 2, 1u);
            bloomHeight = std::max(bloomHeight / 2, 1u);
        }
    }
    ResourceHandle postLdr;
    if (features.Fxaa)
        postLdr = graph.Create(Texture("post.ldr", "RGBA16F", width, height, 8));
    auto backbufferDesc = Texture("output.backbuffer", "SRGB8", width, height, 4);
    // The previous contents are discarded. Undefined is valid for the first
    // acquired image and for images that were presented in an earlier frame.
    backbufferDesc.InitialState = ResourceState::Undefined;
    backbufferDesc.InitialStage = PipelineStage::None;
    const ResourceHandle backbuffer = graph.Import(std::move(backbufferDesc));

    auto environmentPass =
        graph.AddPass("environment", "Environment / IBL Update");
    environmentPass.SetPhase(PassPhase::Prepare)
        .SetSideEffect()
        .Write(environmentMap, ResourceState::ShaderWrite, PipelineStage::Compute)
        .Write(irradianceMap, ResourceState::ShaderWrite, PipelineStage::Compute)
        .Write(prefilterMap, ResourceState::ShaderWrite, PipelineStage::Compute)
        .Write(brdfLut, ResourceState::ShaderWrite, PipelineStage::Compute)
        .SetExecute(Callback(callbacks, RendererPass::Environment));

    auto directionalPass =
        graph.AddPass("directional_shadows", "Shadows/Directional");
    directionalPass.SetEnabled(features.SunShadows)
        .SetSideEffect()
        .SetExecute(Callback(callbacks, RendererPass::DirectionalShadows));
    for (ResourceHandle shadow : directional)
        directionalPass.Write(shadow, ResourceState::DepthWrite,
                              PipelineStage::LateDepth);

    graph.AddPass("local_shadows", "Shadows/Local lights")
        .SetEnabled(features.LocalShadows)
        .SetSideEffect()
        .Read(cookies, ResourceState::ShaderRead, PipelineStage::Fragment)
        .Write(localShadow, ResourceState::DepthWrite, PipelineStage::LateDepth)
        .Write(pointShadow, ResourceState::DepthWrite, PipelineStage::LateDepth)
        .SetExecute(Callback(callbacks, RendererPass::LocalShadows));

    auto mainPass = graph.AddPass("main_hdr", "Main HDR");
    mainPass.SetSideEffect()
        .After("environment")
        .Read(environmentMap)
        .Read(irradianceMap)
        .Read(prefilterMap)
        .Read(brdfLut)
        .Read(cookies)
        .Read(localShadow, ResourceState::DepthRead, PipelineStage::Fragment)
        .Read(pointShadow, ResourceState::DepthRead, PipelineStage::Fragment)
        .Write(hdr, ResourceState::ColorAttachment, PipelineStage::ColorOutput)
        .Write(velocity, ResourceState::ColorAttachment,
               PipelineStage::ColorOutput)
        .Write(depth, ResourceState::DepthWrite, PipelineStage::LateDepth)
        .SetExecute(Callback(callbacks, RendererPass::MainHdr));
    for (ResourceHandle shadow : directional)
        mainPass.Read(shadow, ResourceState::DepthRead, PipelineStage::Fragment);
    if (msaaHdr.Valid())
    {
        mainPass
            .Write(msaaHdr, ResourceState::ColorAttachment,
                   PipelineStage::ColorOutput)
            .Write(msaaVelocity, ResourceState::ColorAttachment,
                   PipelineStage::ColorOutput)
            .Write(msaaDepth, ResourceState::DepthWrite, PipelineStage::LateDepth);
    }

    auto postPass = graph.AddPass("post_process", "Post process");
    postPass.SetSideEffect()
        .Read(hdr, ResourceState::ShaderRead, PipelineStage::AllShaders)
        .Write(backbuffer, ResourceState::ColorAttachment,
               PipelineStage::ColorOutput)
        .SetExecute(Callback(callbacks, RendererPass::PostProcess));
    if (features.TemporalAA)
        postPass.Read(velocity).Read(depth, ResourceState::DepthRead,
                                     PipelineStage::Fragment);
    if (features.TemporalAA)
    {
        const uint32_t readIndex = features.TaaReadIndex % 2;
        const uint32_t writeIndex = features.TaaWriteIndex % 2;
        postPass.Read(taaColor[readIndex]).Read(taaDepth[readIndex]);
        if (writeIndex != readIndex)
            postPass
                .Write(taaColor[writeIndex], ResourceState::ColorAttachment,
                       PipelineStage::ColorOutput)
                .Write(taaDepth[writeIndex], ResourceState::ColorAttachment,
                       PipelineStage::ColorOutput);
    }
    for (ResourceHandle resource : bloom)
        postPass.Write(resource, ResourceState::ShaderWrite,
                       PipelineStage::Compute);
    if (postLdr.Valid())
        postPass.Write(postLdr, ResourceState::ColorAttachment,
                       PipelineStage::ColorOutput);

    graph.AddPass("debug_ui", "Debug UI")
        .SetEnabled(features.DebugUi)
        .SetSideEffect()
        .Write(backbuffer, ResourceState::ColorAttachment,
               PipelineStage::ColorOutput)
        .SetExecute(Callback(callbacks, RendererPass::DebugUi));

    return graph.Compile(error);
}

} // namespace engine::rendergraph
